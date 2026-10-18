#include "Agit.h"
#include "AirShip.h"
#include "Area.h"
#include "Auth.h"
#include "Boss.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "Castle.h"
#include "Creature.h"
#include "CreatureCommon.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "Dominion.h"
#include "Doppelganger.h"
#include "Duel.h"
#include "DynamicQuest.h"
#include "EventCampaign.h"
#include "Fishing.h"
#include "Fortress.h"
#include "GameEventSystem.h"
#include "HTMLCache.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "L2Time.h"
#include "Mentor.h"
#include "MentorSetting.h"
#include "MiniGameAgit.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "NewAction.h"
#include "NewActionHelper.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadMatch.h"
#include "OlympiadStuff.h"
#include "PCParam.h"
#include "Packet.h"
#include "ParserExpData.h"
#include "Party.h"
#include "Path.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Portable/ProcessRuntime.h"
#include "Portable/RuntimeTimingTrace.h"
#include "Quest.h"
#include "Recipe.h"
#include "Residence.h"
#include "SSQ.h"
#include "SerializedMessage.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "SharedContainer.h"
#include "Shuttle.h"
#include "SkillAcquire.h"
#include "SkillFxFunc.h"
#include "StaticObject.h"
#include "ThreadLocalWorldMap.h"
#include "Trait.h"
#include "Trap.h"
#include "User.h"
#include "UserSocket.h"
#include "Vehicle.h"
#include "WorldCollision.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string_view>
#include <vector>
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
volatile std::int32_t g_nHeartbeatingCreatureNum = 0;
volatile std::int32_t g_nExistingCreatureNum = 0;
bool g_CheckCollisionForRangeSkill = true;

// Current L2Server .rdata 0x00f1a730..0x00f1a97f. ValidateSkillMod
// applies these lower bounds to the 74 ModName entries in number order.
const double CCreatureSkillMod::m_ModMinPer[CCreatureSkillMod::MaxModName] = {
    0.5, // PAttack
    0.5, // PDefend
    0.5, // MAttack
    0.5, // MDefend
    0.1, // HPRegen
    0.1, // MPRegen
    0.1, // CPRegen
    0.1, // Speed
    0.7, // AttackSpeed
    0.7, // MagicSpeed
    0.5, // PHit
    0.5, // PAvoid
    0.5, // MHit
    0.5, // MAvoid
    0.1, // CriticalProb
    0.1, // MagicCriticalProb
    0.5, // MaxHP
    0.5, // MaxMP
    0.5, // MaxCP
    0.5, // Range
    0.1, // CriticalDamage
    0.1, // CancelDamage
    0.7, // AttackSpeedByWeapon
    0.5, // SafeHeight
    0.5, // Breath
    0.1, // AreaDamage
    0.5, // HitBonusAtNight
    0.1, // CriticalPositionFront
    0.1, // CriticalPositionSide
    0.1, // CriticalPositionBack
    0.1, // CriticalDamagePositionFront
    0.1, // CriticalDamagePositionSide
    0.1, // CriticalDamagePositionBack
    0.1, // MPCost0
    0.1, // MPCost1
    0.1, // MPCost2
    0.1, // MPCost3
    0.1, // MPCost4
    0.1, // MPCost5
    0.1, // MPCost6
    0.1, // MPCost7
    0.1, // FatalBlow
    0.1, // Shield
    0.1, // SkillCritical
    0.1, // HealBonus
    0.1, // DefenceCriticalProb
    0.1, // DefenceCriticalDamage
    0.1, // DefenceDDResist
    0.1, // PSkillPower
    0.1, // MagicDefenceCriticalProb
    0.1, // MagicDefenceCriticalDamage
    0.1, // MagicCriticalDamage
    0.1, // WeightLimit
    0.1, // WeightPenalty
    0.1, // PSpellPower
    0.1, // PAbnormalResist
    0.1, // MAbnormalResist
    0.1, // MCancelResist
    -1000.0, // DefenceDamagePositionFront
    0.1, // DefenceDamagePositionSide
    0.1, // DefenceDamagePositionBack
    0.1, // HateBonus
    0.1, // PhysicalSkillCriticalProbByStat
    0.1, // PositionDamageFront
    0.1, // PositionDamageSide
    0.1, // PositionDamageBack
    0.1, // BlockActBonusSelf
    0.1, // BlockActBonusTarget
    0.1, // PotionBonus
    -1000.0, // PhysicalSkillCriticalProb
    -1000.0, // PhysicalSkillCriticalDamage
    0.0, // HitSummon
    0.0, // EnergyAttack
    0.0, // HitNumber
};

namespace {

// Current Creature_server.obj local .rdata at 0x00f1aa90. The aliases
// 6/7 -> 5 and 9/10 -> 8 are the target's crystal-grade normalization used
// when equipping arrows and bolts.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

// Current L2Server .rdata 0x00f1aae0 (duplicated at 0x01071b90).
constexpr std::int32_t kUltimateSkillLevelGauge[4] = {
    0,
    500,
    1000,
    1500,
};

struct CosSinTable {
    CosSinTable() {
        for (std::size_t index = 0; index < 12; ++index) {
            const double angle = static_cast<double>(index) * 0.5233333333333333;
            m_cosTable[index] = std::cos(angle);
            m_sinTable[index] = std::sin(angle);
        }
    }

    double m_cosTable[12];
    double m_sinTable[12];
};


CosSinTable g_cosSinTable;

constexpr std::size_t kMaxLookNeighborNpcIds = 0x139c;
thread_local std::array<std::int32_t, kMaxLookNeighborNpcIds> g_lookNeighborNpcIds{};
thread_local std::int32_t g_lookNeighborNpcIdCount = 0;


int NormalizeSkillMagicType(int magicType) noexcept {
    if (magicType >= SIMT_PHYSICAL_ATTACK_SKILL && magicType <= SIMT_PHYSICAL_ENCHANT_SKILL) {
        return SIMT_PHYSICAL_SKILL;
    }
    if (magicType >= SIMT_MAGIC_ELEMENT_SKILL && magicType <= SIMT_MAGIC_VOODOO_SKILL) {
        return SIMT_MAGIC_SKILL;
    }
    return magicType;
}

struct InfectedSkillBuffSort {
    bool operator()(const InfectedSkillType& left, const InfectedSkillType& right) const noexcept {
        const int leftType = NormalizeSkillMagicType(left.pSkillInfo->m_nIsMagic);
        const int rightType = NormalizeSkillMagicType(right.pSkillInfo->m_nIsMagic);

        if ((leftType == SIMT_TRIGGER_SKILL) != (rightType == SIMT_TRIGGER_SKILL)) {
            return rightType == SIMT_TRIGGER_SKILL;
        }
        if ((leftType == SIMT_SONGDANCE_SKILL) != (rightType == SIMT_SONGDANCE_SKILL)) {
            return rightType == SIMT_SONGDANCE_SKILL;
        }
        return left.pSkillInfo->m_eDebuff < right.pSkillInfo->m_eDebuff;
    }
};

const void* DoubleBitsAsPointer(double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(bits));
}


} // namespace

CSPointer<CCreature> CCreature::FindObjectSP(std::uint32_t smartId) {
    CSPointer<CCreature> result;
    result.SetObject(g_CreatureSmartIdManager.GetObjectW(smartId), ObjectRefType::ObjectIndex);
    return result;
}

void CCreature::Unfreeze() {
    L2_GUARD_FUNCTION();
    if (m_bIsFreezing) {
        m_bIsFreezing = false;
        runtime::raw_atomic::IncrementI32(&g_nHeartbeatingCreatureNum);
    }
}

void CCreature::TimerExpired(int id) {
    // Current L2Server 0x0061e660. This is the single creature heartbeat.
    // the timer id is ignored by the original body.
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
    L2_DECLARE_AUTO_TIMER(timer, JOB_CCREATURE__TIMEREXPIRED);

    m_nTickCount = runtime::AddInt32Wrapping(m_nTickCount, 1);
    if (m_bDeleting || m_Deleted != 0) {
        return;
    }
    if (IsActive()) {
        HandleSerializedMessages();
    }
    if (FreezeIfNobodyNearMe()) {
        return;
    }
    Unfreeze();

    FVector position = IsPC() ? GetDetailLocation() : GetPos();
    const double deltaX = position.x - m_vPosOnTick.x;
    const double deltaY = position.y - m_vPosOnTick.y;
    const double deltaZ = position.z - m_vPosOnTick.z;
    const int movedDistance =
        runtime::Win64CvttDoubleToInt32(std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ));
    m_vPosOnTick = position;

    CGeoZone* zone = nullptr;
    CGeoSector* sector = nullptr;
    CGeoData* const geo = CWorld::Inst().GetGeo();
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    zone = geo->GetZone(
        (runtime::AddInt32Wrapping(x, 0x50000) >> 15) + 10, (runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10
    );
    if (zone != nullptr) {
        sector = zone->GetSector((x >> 7) & 0xff, (y >> 7) & 0xff);
    }
    if (zone != nullptr && sector != nullptr) {
        AreaCheckForEveryTick(zone, sector, movedDistance);
        if (m_data->m_bIsFreezed != m_AreaMod.m_bWasFrozen) {
            if (m_data->m_bIsFreezed) {
                StopMove();
            } else {
                GetController()->SetJustUnfrozen();
            }
            SendFreezeStatusMessage();
        }
    }

    if (IsNPC() && m_data->m_bIsAlive != 0 && m_data->IsSummon()) {
        CNPC* const npc = CastNPC();
        CSummon* const summon = CastSummon();
        if (npc == nullptr || summon == nullptr) {
            // Log.Add(LOG_ERROR, L"What the hell!!!")
            // Log.Add(
            //     LOG_ERROR,
            //     "[creature] Summon lifetime update skipped for object %u: NPC cast present %s, summon cast present %s.
            //     continuing the heartbeat.", m_nId, npc != nullptr ? "yes" : "no", summon != nullptr ? "yes" : "no")
            L2_LOG_ERROR(
                Log,
                "[creature] Summon lifetime update skipped for object %u: NPC cast present %s, summon cast present %s; "
                "continuing the heartbeat.",
                m_nId, npc != nullptr ? "yes" : "no", summon != nullptr ? "yes" : "no"
            );
        } else {
            const std::uint32_t now = runtime::GetTickCount();
            const std::int32_t previous = runtime::Int32FromBits(npc->m_nSummonLifeLastTick);
            m_nSummonLifeLastTick = now;
            if (!m_data->m_bIsInCombatMode && summon->m_data->m_Race != RT_SIEGE_WEAPON) {
                const std::int32_t elapsed = runtime::MultiplyInt32Wrapping(
                    runtime::SubtractInt32Wrapping(runtime::Int32FromBits(now), previous), 3
                );
                const std::int32_t adjusted = runtime::AddInt32Wrapping(elapsed, (elapsed >> 31) & 3);
                m_data->m_dwSummonLifeTime += static_cast<std::uint32_t>(adjusted >> 2);
            }
            if (summon->CheckConsumeItemCondition()) {
                summon->ConsumeItem();
            }
        }
    }

    bool combatModeExpired = false;
    bool combatSkillWasActive = false;
    if (m_data->m_bIsInCombatMode && !CIOObject::IsInTime(m_nCombatModeTimeStamp, 15)) {
        combatModeExpired = true;
        SetInCombatMode(false);
        combatSkillWasActive = m_SkillDurationMod.m_CombatSkillCount > 0;
    }

    ReflectDayAndNightEffect();
    CheckOneGiWok();
    CheckLowHPState();
    CheckHighHPState();

    if (m_data->m_bIsAlive == 0) {
        const int deathCount = runtime::AddInt32Wrapping(m_nDeathCount.fetch_add(1, std::memory_order_seq_cst), 1);
        if (m_data->m_bIsDiss && IsNPC() && m_data->m_bExistInHash) {
            LeaveWorld(true);
        }

        const int corpseTime = GetCorpseTime();
        if (runtime::MultiplyInt32Wrapping(corpseTime, 1000) <
            runtime::MultiplyInt32Wrapping(nHeartBeat, deathCount)) {
            if (IsNPC() && m_data->m_bDecayComplete) {
                OnCorpseDecayed();
                return;
            }
            if (IsNPC() && deathCount == 500 && !m_data->m_bDecayComplete) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"NPC is dead but not decayed! "
                //     L"Name[%s] classId[%d] HP[%f]",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_data->m_dHP)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[creature] Corpse decay is incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_nId,
                //     deathCount,
                //     m_data->m_dHP)
                // L2_LOG_ERROR(Log,
                //     L"[creature] Corpse decay is incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_nId,
                //     deathCount,
                //     m_data->m_dHP)
                L2_LOG_ERROR(
                    Log, u"[creature] Corpse decay is incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                    GetName(), m_data->m_nClassID, m_nId, deathCount, m_data->m_dHP
                );
                CNPC* const npc = CastNPC();
                if (npc != nullptr) {
                    CCreature* const resolved = g_CreatureSmartIdManager.GetObjectW(m_nId);
                    CNPC* const resolvedNpc = resolved != nullptr && resolved->IsNPC() ? resolved->CastNPC() : nullptr;
                    if (resolvedNpc != npc) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"FILE[%s] LINE[%d] "
                        //     L"GetObject has problem",
                        //     L"Creature_server.cpp",
                        //     0x541)
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[creature] Corpse registry mismatch for NPC object %u: lookup object present %s, NPC cast
                        //     present %s. Continuing death notification.", m_nId, resolved != nullptr ? "yes" : "no",
                        //     resolvedNpc != nullptr ? "yes" : "no")
                        L2_LOG_ERROR(
                            Log,
                            "[creature] Corpse registry mismatch for NPC object %u: lookup object present %s, NPC cast "
                            "present %s; continuing death notification.",
                            m_nId, resolved != nullptr ? "yes" : "no", resolvedNpc != nullptr ? "yes" : "no"
                        );
                    }

                    User* killerMaster = nullptr;
                    CCreature* const killerMasterObject = g_CreatureSmartIdManager.GetObjectW(npc->m_sidKillerMaster);
                    if (killerMasterObject != nullptr && killerMasterObject->IsPC()) {
                        killerMaster = killerMasterObject->CastUser();
                    }
                    g_npcserver.Send(
                        "cdSd", 5, static_cast<int>(m_nId), killerMaster != nullptr ? killerMaster->GetName() : u"",
                        killerMaster != nullptr ? npc->m_nKillerMemberCount : 0
                    );
                }
            } else if (IsNPC() && deathCount == 1000) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"NPC cannot be decayed "
                //     L"Name[%s] classId[%d] HP[%f]",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_data->m_dHP)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[creature] Corpse decay is still incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_nId,
                //     deathCount,
                //     m_data->m_dHP)
                // L2_LOG_ERROR(Log,
                //     L"[creature] Corpse decay is still incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                //     GetName(),
                //     m_data->m_nClassID,
                //     m_nId,
                //     deathCount,
                //     m_data->m_dHP)
                L2_LOG_ERROR(
                    Log,
                    u"[creature] Corpse decay is still incomplete for %s, class %d, object %u at death tick %d (HP %f).",
                    GetName(), m_data->m_nClassID, m_nId, deathCount, m_data->m_dHP
                );
            }
        } else if (IsPC()) {
            DespawnAllCubic(true);
            ToggleSkillAllOff(true);
            AuraSkillAllOff(true);
        }
    }

    std::vector<CSkillEffect*> tickEffects;
    std::vector<unsigned int> tickSourceIds;
    std::vector<const CSkillInfo*> tickSkills;
    std::vector<int> expiredSkills;
    std::vector<int> reducedSkills;

    {
        L2_GUARD(m_InstantDurationSect);
        if (m_InstantDurationSkill.pSkillInfo != nullptr) {
            CSkillInfo* const skillInfo = m_InstantDurationSkill.pSkillInfo;
            const int abnormalTime = GetAbnormalTime(skillInfo, false);
            if (!CreatureCommonFunc::PushEffectsIfInTime(
                    m_InstantDurationSkill, m_nTickCount, tickEffects, tickSourceIds, tickSkills, 0,
                    static_cast<int>(GetDBID()), abnormalTime
                )) {
                _OnAbnormalStatusDeleted(
                    *skillInfo, this, SPET_TIMEOUT, static_cast<int>(m_InstantDurationSkill.nSourceCreatureId)
                );
                const AbnormalStatusSlotType abnormalType = skillInfo->m_nAbnormalType;
                m_InstantDurationSkill.pSkillInfo = nullptr;
                if (m_data->DeleteAbnormalStatus(abnormalType)) {
                    m_bAbnormalStatusChanged = true;
                    if (IsPC()) {
                        CastUser()->_SendShortBuffStatusInfo();
                    }
                }
            }
        }
    }

    {
        L2_GUARD(m_InfectedSkillsSect);
        for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
            InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
            const int abnormalTime = infected.pSkillInfo->m_nAbnormalTime == -2
                ? infected.nDuration
                : GetAbnormalTime(infected.pSkillInfo, false);
            if (!CreatureCommonFunc::PushEffectsIfInTime(
                    infected, m_nTickCount, tickEffects, tickSourceIds, tickSkills,
                    static_cast<int>(infected.nSourceCreatureId), static_cast<int>(GetDBID()), abnormalTime
                )) {
                expiredSkills.push_back(static_cast<int>(index));
            }
        }
        for (std::size_t position = expiredSkills.size(); position-- > 0;) {
            const int index = runtime::CheckedVectorAt(expiredSkills, position);
            const CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            _DeleteAbnormalStatus(index, this, skillInfo, true, SPET_TIMEOUT, false);
        }

        for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
            if (IsReduceSkill(runtime::CheckedVectorAt(m_InfectedSkills, index))) {
                reducedSkills.push_back(static_cast<int>(index));
            }
        }
        for (std::size_t position = reducedSkills.size(); position-- > 0;) {
            const int index = runtime::CheckedVectorAt(reducedSkills, position);
            const CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            _DeleteAbnormalStatus(index, this, skillInfo, true, SPET_REDUCE, false);
        }
    }

    int changedSkillCount = static_cast<int>(expiredSkills.size());
    {
        GuardHelper toggleSkillGuard(L"creature timer expired 11 - toggle skill", CThreadManager::GetThreadID());
        L2_GUARD(m_SkillListSect);
        const auto processSkill = [this, &tickEffects, &tickSourceIds, &tickSkills, &changedSkillCount](
                                      std::pair<const CSkillInfo*, bool>& state, const SkillPumpEndType pumpEndType
                                  ) {
            const CSkillInfo* const skillInfo = state.first;
            if (!state.second || skillInfo == nullptr) {
                return;
            }

            CSkillEffect* const consumeEffect = skillInfo->m_pConsumeEffect;
            if (consumeEffect != nullptr) {
                const int unitTick = consumeEffect->nUnitTick;
                if (unitTick == 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Abnormal skill tick!!! "
                    //     "skill number is %d",
                    //     skillInfo->m_nSkillId)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[skills] Skill %d has a zero consumption interval on object %u. Skipping resource consumption and
                    //     continuing effect-tick checks.", skillInfo->m_nSkillId, m_nId)
                    L2_LOG_ERROR(
                        Log,
                        "[skills] Skill %d has a zero consumption interval on object %u; skipping resource consumption and "
                        "continuing effect-tick checks.",
                        skillInfo->m_nSkillId, m_nId
                    );
                } else if (m_nTickCount % unitTick == 0 &&
                    !consumeEffect->Consume(
                        this, static_cast<double>(runtime::MultiplyInt32Wrapping(unitTick, nHeartBeat)) / 1000.0
                    )) {
                    PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, pumpEndType));
                    state.second = false;
                    ++changedSkillCount;
                    if (skillInfo->m_nToggleGroupId > 0) {
                        m_GroupToggleSkills.erase(skillInfo->m_nToggleGroupId);
                    }
                }
            }

            for (CSkillEffect* const effect : skillInfo->m_TickEffects) {
                CSkillTickEffect* const tickEffect = effect->CastTickEffect();
                if (tickEffect == nullptr || tickEffect->nUnitTick == 0 || m_nTickCount % tickEffect->nUnitTick != 0) {
                    continue;
                }
                tickEffects.push_back(tickEffect);
                tickSourceIds.push_back(m_nId);
                tickSkills.push_back(skillInfo);
            }
        };

        for (auto& toggle : m_ToggleSkills) {
            processSkill(toggle.second, SPET_TOGGLE);
        }
        for (auto& aura : m_AuraSkills) {
            processSkill(aura.second, SPET_AURA);
        }
    }

    for (std::size_t index = 0; index < tickEffects.size(); ++index) {
        CSkillEffect* const effect = runtime::CheckedVectorAt(tickEffects, index);
        effect->Tick(
            static_cast<CWorldObject*>(this),
            static_cast<double>(runtime::MultiplyInt32Wrapping(effect->nUnitTick, nHeartBeat)) / 1000.0,
            runtime::CheckedVectorAt(tickSourceIds, index), runtime::CheckedVectorAt(tickSkills, index)
        );
    }

    if (m_data->m_bIsAlive == 0) {
        AddTimer(nHeartBeat, 0);
        if (m_bAbnormalStatusChanged) {
            {
                L2_GUARD(m_InfectedSkillsSect);
                _BroadCastAbnormalStatusToUsersTargetMe();
            }
            if (IsPC()) {
                CastUser()->SendAbnormalStatusInfo();
            }
            m_bAbnormalStatusChanged = false;
        }
        return;
    }
    if (IsPC() && !CastUser()->IsAliveInOlympiad()) {
        AddTimer(nHeartBeat, 0);
        return;
    }
    if (m_bLeft) {
        return;
    }

    if (combatModeExpired) {
        BroadcastCombatModeFinish();
        CheckSummonBattleMode();
    }
    CubicActionTimer();
    if ((m_SkillDurationMod.m_nFearCount > 0 || m_SkillDurationMod.m_nPsychicPowerCount > 0) && m_nTickCount % 3 == 0) {
        FearAction(m_SkillDurationMod.m_nFearCount > 0);
    }

    GeoFieldEffect(zone, sector, movedDistance, position);

    if (m_nEnergyCount > 0 && !CIOObject::IsInShortTime(m_dwEnergyStartTime, 600000)) {
        {
            L2_GUARD(*m_lock);
            m_nEnergyCount = 0;
        }
        if (IsPC()) {
            CastUser()->SendETCBuffStatusInfo();
        }
    }
    CheckCubicExpired();

    {
        GuardHelper regenerationGuard(L"creature timer expired 14 - regen", CThreadManager::GetThreadID());
        if (changedSkillCount > 0 || combatSkillWasActive) {
            if (IsPC()) {
                CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
            } else {
                ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
            }
            m_bAbnormalStatusChanged = true;
            CheckAbnormalVisualEffect();
            m_data->ValidateMP(0.0);
            if (IsPC()) {
                m_data->ValidateCP(0.0);
            }
        }

        if (m_nTickCount % 5 == 0 && !m_bIgnoreRegeneration) {
            OnRegenTimer(position);
        }
    }

    if (m_ShowPosTarget != 0) {
        CCreature* const target = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_ShowPosTarget));
        User* const user = target != nullptr && target->IsPC() ? target->CastUser() : nullptr;
        if (user != nullptr) {
            static FVector previousPosition{};
            if (!(previousPosition == m_data->m_Pos)) {
                previousPosition = m_data->m_Pos;
                std::vector<FVector> points;
                points.push_back(m_data->m_Pos);
                user->ShowTraceEmiter(0, points, 30000);
            }
        }
    }

    MesCheck();
    ApplyMPReward();

    if (!IsPC() && m_bValidateExChanged) {
        const int parameterMask = runtime::raw_atomic::ExchangeI32(&m_nValidateExChanged, 0);
        m_bValidateExChanged = false;
        // Current L2Server.exe CCreature::TimerExpired at 0x0061fd16
        // calls virtual slot +0xA90 here: ValidateExParameters(mask).
        // Re-queueing through ValidateExChanged leaves the pending mask in a
        // permanent loop instead of consuming it on the heartbeat.
        ValidateExParameters(parameterMask);
    }
    if (!IsPC() && m_bValidateChanged && !m_lockValidate.IsEntered()) {
        const int parameterMask = runtime::raw_atomic::ExchangeI32(&m_nValidateChanged, 0);
        m_bValidateChanged = false;
        // Current L2Server.exe CCreature::TimerExpired at 0x0061fd6d
        // calls virtual slot +0x718 with (mask, false):
        // ValidateParameters(mask, false). This is what applies a queued
        // move-mode change and recomputes the active speed lane.
        ValidateParameters(parameterMask, false);
    }

    if (m_bAbnormalStatusChanged) {
        {
            L2_GUARD(m_InfectedSkillsSect);
            _BroadCastAbnormalStatusToUsersTargetMe();
        }
        if (IsPC()) {
            CastUser()->SendAbnormalStatusInfo();
        }
        m_bAbnormalStatusChanged = false;
    }
    AddTimer(nHeartBeat, 0);
}

void CCreature::DoNothing() { GetController()->DoNothing(true); }

bool CCreature::GetBlinkDestPos(const double distance, const SkillDirectionType direction, FVector& destination) {
    // Current L2Server 0x005f7904.
    L2_GUARD_FUNCTION();

    const FVector source = GetPos();
    FVector facing = GetNormalizedDirVector(0);
    if (direction == DT_BACK) {
        facing.x = -facing.x;
        facing.y = -facing.y;
        facing.z = -facing.z;
    } else if (direction != DT_FRONT) {
        return false;
    }

    const FVector blinkOrigin{source.x + facing.x, source.y + facing.y, source.z + facing.z};
    FVector blinkDirection{};
    if (!m_ChargeData.IsZero()) {
        blinkDirection = m_ChargeData.GetDir();
    } else {
        blinkDirection = FVector{blinkOrigin.x - source.x, blinkOrigin.y - source.y, blinkOrigin.z - source.z};
        const double length = blinkDirection.Size();
        constexpr double kCurrentEpsilon = static_cast<double>(1.0e-5F);
        if (length <= -kCurrentEpsilon || kCurrentEpsilon <= length) {
            blinkDirection.x /= length;
            blinkDirection.y /= length;
            blinkDirection.z /= length;
        } else {
            blinkDirection = FVector{1.0, 1.0, 1.0};
        }
    }
    blinkDirection.x *= distance;
    blinkDirection.y *= distance;
    blinkDirection.z *= distance;
    destination =
        FVector{blinkOrigin.x + blinkDirection.x, blinkOrigin.y + blinkDirection.y, blinkOrigin.z + blinkDirection.z};

    const bool floating = m_data->IsFlying() || m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(source, destination, floating)) {
        return false;
    }

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            source, destination, 0.0, collisionPosition, static_cast<int>(GetInZoneID()), false
        )) {
        const double collisionHeight = GetCollisionHeight() + 10.0;
        blinkDirection.NormalizeCube();
        destination = FVector{collisionPosition.x - collisionHeight * blinkDirection.x,
            collisionPosition.y - collisionHeight * blinkDirection.y,
            collisionPosition.z - collisionHeight * blinkDirection.z};
    }

    bool distanceExhausted = true;
    double travelledDistance = 0.0;
    if (!world.CanGo(
            source, destination, collisionPosition, distanceExhausted, travelledDistance, static_cast<int>(GetInZoneID()),
            false
        )) {
        destination = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }
    return true;
}

bool CCreature::GetBlinkDestPos(const double distance, const FVector& direction, FVector& destination) {
    // Current L2Server 0x005f817c.
    L2_GUARD_FUNCTION();

    const FVector source = GetPos();
    FVector blinkDirection = direction;
    blinkDirection.NormalizeCube();
    blinkDirection.x *= distance;
    blinkDirection.y *= distance;
    blinkDirection.z *= distance;
    destination = FVector{source.x + blinkDirection.x, source.y + blinkDirection.y, source.z + blinkDirection.z};

    const bool floating = m_data->IsFlying() || m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(source, destination, floating)) {
        return false;
    }

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            source, destination, 0.0, collisionPosition, static_cast<int>(GetInZoneID()), false
        )) {
        const double collisionHeight = GetCollisionHeight() + 10.0;
        blinkDirection.NormalizeCube();
        destination = FVector{collisionPosition.x - collisionHeight * blinkDirection.x,
            collisionPosition.y - collisionHeight * blinkDirection.y,
            collisionPosition.z - collisionHeight * blinkDirection.z};
    }

    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    if (!world.CanGo(
            source, destination, collisionPosition, distanceExhausted, travelledDistance, static_cast<int>(GetInZoneID()),
            false
        )) {
        destination = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }
    return true;
}

bool CCreature::GetAttackFrontPos(CCreature* target, FVector& position, FAngle& direction) {
    // Current L2Server 0x005f7e64.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    const FVector sourcePosition = GetPos();
    const double separation = target->GetCollisionRadius() + GetCollisionRadius() + 10.0;
    const FVector targetPosition = target->GetPos();
    FVector forward = GetNormalizedDirVector(0);
    FVector destination{sourcePosition.x + separation * forward.x, sourcePosition.y + separation * forward.y,
        sourcePosition.z + separation * forward.z};

    const bool floating = m_data->m_moveType == CMT_FLY || m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(sourcePosition, destination, floating)) {
        return false;
    }

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            sourcePosition, destination, 0.0, collisionPosition, static_cast<int>(GetInZoneID()), false
        )) {
        const double collisionRadius = GetCollisionRadius() + 10.0;
        forward.NormalizeCube();
        destination = FVector{collisionPosition.x - collisionRadius * forward.x,
            collisionPosition.y - collisionRadius * forward.y, collisionPosition.z - collisionRadius * forward.z};
    }

    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    if (world.CanGo(
            targetPosition, destination, collisionPosition, distanceExhausted, travelledDistance,
            static_cast<int>(GetInZoneID()), false
        ) ||
        travelledDistance >= 16.0) {
        position = destination;
        direction = GetDir();
        return true;
    }

    position = collisionPosition;
    return false;
}

bool CCreature::BlinkReturnPos(FVector& destination) {
    // Current L2Server 0x006097fc.
    L2_GUARD_FUNCTION();

    const FVector source = GetPos();
    FVector facing = GetNormalizedDirVector(0);
    const bool floating = m_data->IsFlying() || m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(source, destination, floating)) {
        return false;
    }

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            source, destination, 0.0, collisionPosition, static_cast<int>(GetInZoneID()), false
        )) {
        facing.NormalizeCube();
        destination =
            FVector{collisionPosition.x - facing.x, collisionPosition.y - facing.y, collisionPosition.z - facing.z};
    }

    bool distanceExhausted = true;
    double travelledDistance = 0.0;
    if (!world.CanGo(
            source, destination, collisionPosition, distanceExhausted, travelledDistance, static_cast<int>(GetInZoneID()),
            false
        )) {
        destination = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }

    if (!world.MoveTo(this, destination, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWorld::Inst().MoveTo Failed in i_blink_return")
        // Log.Add(
        //     LOG_ERROR,
        //     "[movement] Blink return failed for object %u: MoveTo rejected destination (%.3f, %.3f, %.3f).",
        //     m_nId,
        //     destination.x,
        //     destination.y,
        //     destination.z)
        L2_LOG_ERROR(
            Log, "[movement] Blink return failed for object %u: MoveTo rejected destination (%.3f, %.3f, %.3f).", m_nId,
            destination.x, destination.y, destination.z
        );
        return false;
    }

    BroadcastFlyToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), FTLT_BLINK, 0, 0, 0
    );
    ValidateLocation();
    return true;
}

bool CCreature::GetTargetBackPos(CCreature* target, FVector& position) {
    // Current L2Server 0x005efb0c.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    const FVector sourcePosition = GetPos();
    const double separation = target->GetCollisionRadius() + GetCollisionRadius() + 10.0;
    const FVector targetPosition = target->GetPos();
    const FVector targetBack = target->GetNormalizedDirVector(180);
    FVector destination{targetPosition.x + separation * targetBack.x, targetPosition.y + separation * targetBack.y,
        targetPosition.z + separation * targetBack.z};

    FVector collisionPosition{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    if (!CWorld::Inst().CanGo(
            sourcePosition, destination, collisionPosition, distanceExhausted, travelledDistance, GetInZoneID(), false
        )) {
        position = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }
    position = destination;
    return true;
}

bool CCreature::GetSidestepPos(CCreature* target, FVector& position, const SkillFxPositionType positionType) {
    // Current L2Server 0x005f843c.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    const FVector sourcePosition = GetPos();
    const double separation = target->GetCollisionRadius() + GetCollisionRadius() + 10.0;
    const FVector targetPosition = target->GetPos();
    FVector forward{
        targetPosition.x - sourcePosition.x, targetPosition.y - sourcePosition.y, targetPosition.z - sourcePosition.z};
    forward.NormalizeSquare();

    const FVector axis{0.0, 0.0, 1.0};
    FVector side{};
    if (positionType == SFPT_LEFT) {
        side = FVector::RotateAngleAxis(&forward, 0xc000, &axis);
    } else if (positionType == SFPT_RIGHT) {
        side = FVector::RotateAngleAxis(&forward, 0x4000, &axis);
    } else {
        return false;
    }
    side.x *= separation;
    side.y *= separation;
    side.z *= separation;

    FVector destination{targetPosition.x + side.x, targetPosition.y + side.y, targetPosition.z + side.z};
    FVector collisionPosition{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    if (!CWorld::Inst().CanGo(
            sourcePosition, destination, collisionPosition, distanceExhausted, travelledDistance, GetInZoneID(), false
        )) {
        position = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }
    position = destination;
    return true;
}

int CCreature::GetPrivateID() const {
    // Current L2Server 0x005eda58.
    return m_data->m_nPrivateID;
}

void CCreature::SetPrivateID(const int privateId) {
    // Current L2Server 0x00608f04.
    L2_GUARD_FUNCTION();

    m_data->m_nPrivateID = privateId;
    if (!IsPC()) {
        return;
    }

    User* const user = CastUser();
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(user, summon, { summon->SetPrivateID(privateId); });
}

FVector CCreature::GetDetailLocation() {
    if (!m_bMoving) {
        return m_data->GetPos();
    }

    std::int32_t elapsed = 0x320;
    const std::uint32_t firstElapsed = runtime::GetTickCount() - m_nLastTick;
    if (firstElapsed <= 0x320U) {
        elapsed = runtime::Int32FromBits(runtime::GetTickCount() - m_nLastTick);
    }
    const std::int32_t moveTick = runtime::Int32FromBits(m_nMoveTick);
    if (elapsed >= moveTick) {
        elapsed = moveTick;
    }

    const double elapsedSeconds = static_cast<double>(elapsed) / 1000.0;
    const FVector position = m_data->GetPos();
    return FVector{elapsedSeconds * m_vMovingDirection.x + position.x, elapsedSeconds * m_vMovingDirection.y + position.y,
        elapsedSeconds * m_vMovingDirection.z + position.z};
}

void CCreature::TurnDirToTarget(CWorldObject& target) {
    const FVector position = GetPos();
    FVector targetPosition{};
    if (target.IsCreature()) {
        targetPosition = target.CastCreature()->GetDetailLocation();
    } else {
        targetPosition = target.GetPos();
    }
    if (target.IsAirShipPart()) {
        targetPosition.z = position.z;
    }

    FAngle direction;
    direction.Yaw = runtime::Win64CvttDoubleToInt32(
        (std::atan2(targetPosition.y - position.y, targetPosition.x - position.x) * 65535.0) / 6.283185307179586
    );
    m_data->SetDir(direction);
}

void CCreature::SetDir(const FAngle& direction) {
    L2_GUARD_FUNCTION();
    m_data->SetDir(direction);
    if (m_bShowMovePosToGM) {
        ShowLoc(nullptr);
    }
}

void CCreature::ValidateLocation() {
    L2_GUARD_FUNCTION();
    const int vehicleId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    const int airShipId = static_cast<int>(GetMyAirShipID());
    const int shuttleId = CContainerDB::Inst().GetShuttleID(m_data->container_index);

    if (vehicleId > 0) {
        ValidateLocationInVehicle(static_cast<unsigned int>(vehicleId));
        return;
    }
    if (airShipId > 0) {
        ValidateLocationInAirShip(static_cast<unsigned int>(airShipId));
        return;
    }
    if (shuttleId > 0) {
        ValidateLocationInShuttle(static_cast<unsigned int>(shuttleId));
        return;
    }

    const FAngle direction = m_data->GetDir();
    const FVector position = m_data->GetPos();
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    const int yaw = direction.Yaw & 0xffff;
    if (!IsPC()) {
        g_ptlUserMap->BroadcastToNeighborContainer(
            this, 0x5000, position, "cdddddc", 0x79, static_cast<int>(m_nId), x, y, z, yaw, -1
        );
    } else if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborExceptSelf(
            this, 0x5000, position, "cdddddc", 0x79, static_cast<int>(m_nId), x, y, z, yaw, -1
        );
    }
}

void CCreature::ValidateLocationInAirShip(unsigned int airShipId) {
    L2_GUARD_FUNCTION();
    if (airShipId == 0) {
        return;
    }
    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (airShip != nullptr) {
        CreatureCommonFunc::BroadcastValidateLocation(*this, *airShip, 0x70);
    }
}

void CCreature::ValidateLocationInVehicle(unsigned int vehicleId) {
    L2_GUARD_FUNCTION();
    if (vehicleId == 0) {
        return;
    }
    CSPointer<CVehicle> vehicle = CVehicle::FindObjectSP(vehicleId);
    if (vehicle) {
        CreatureCommonFunc::BroadcastValidateLocation(*this, *vehicle.get(), 0x80);
    }
}

void CCreature::ValidateLocationInShuttle(unsigned int shuttleId) {
    L2_GUARD_FUNCTION();
    if (shuttleId == 0) {
        return;
    }
    CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(shuttleId);
    if (shuttle != nullptr) {
        CreatureCommonFunc::BroadcastValidateLocation(*this, *shuttle, 0xd1);
    }
}

double CCreature::GetRemainReuseDelaySec(int skillId, int level, int subLevel) {
    L2_GUARD_FUNCTION();
    const std::uint32_t endpoint = m_SkillReuseDelay.GetSkillUsedTime(skillId, level, subLevel);
    if (endpoint == 0) {
        return 0.0;
    }

    const std::uint32_t differenceBits = endpoint - runtime::GetTickCount();
    std::int32_t difference = 0;
    std::memcpy(&difference, &differenceBits, sizeof(difference));
    if (difference <= 0) {
        return 0.0;
    }
    return static_cast<double>(difference) / 1000.0;
}

int CCreature::CalcReuseDelay(CSkillInfo& skillInfo) {
    // Current L2Server 0x005ecd34.
    L2_GUARD_FUNCTION();
    int reuseDelay = skillInfo.GetNormalReuseDelay(this);
    const int magicType = skillInfo.m_nIsMagic;
    if (magicType == SIMT_ETC_SKILL || skillInfo.m_bReuseDelayLock ||
        skillInfo.m_OperateType == SKILL_RACE_SPECIAL_PASSIVE || magicType == SIMT_PHYSICAL_SKILL_NOCANCEL ||
        magicType == SIMT_MAGIC_SKILL_NOCANCEL || skillInfo.m_OperateType != SKILL_INSTANT) {
        return reuseDelay;
    }

    double statBonus = 0.0;
    switch (m_SkillMod.m_eParamType) {
        case STE_STR:
            statBonus = StrBonusF();
            break;
        case STE_DEX:
            statBonus = DexBonusF();
            break;
        case STE_CON:
            statBonus = ConBonusF();
            break;
        case STE_INT:
            statBonus = IntBonusF();
            break;
        case STE_MEN:
            statBonus = MenBonusF();
            break;
        case STE_WIT:
            statBonus = WitBonusF();
            break;
        default:
            break;
    }

    double probability = statBonus * m_SkillMod.m_Mods[CCreatureSkillMod::SkillCritical].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::SkillCritical].m_Diff;
    if (magicType == SIMT_MAGIC_SKILL || (magicType >= 20 && magicType <= 25)) {
        probability *= 0.5;
    }
    if (::genrand64_real1() * 100.0 < probability) {
        SendSystemMessage(SystemMessageId::SKILL_MASTERY_ALLOWS_IMMEDIATE_REUSE);
        reuseDelay = 0;
    }
    return reuseDelay;
}

int CCreature::SetSkillReuseDelay(CSkillInfo& skillInfo) {
    // Current L2Server 0x005fdadc.
    L2_GUARD_FUNCTION();
    const int reuseDelay = CalcReuseDelay(skillInfo);
    if (reuseDelay > 0) {
        const int subjobId = m_data->m_nSubJobId;
        const int groupId = skillInfo.m_nReuseDelayGroup;

        auto groupedSkillId = [&]() {
            int selectedSkillId = skillInfo.m_nSkillId;
            if (groupId <= 0) {
                return selectedSkillId;
            }
            L2_GUARD(m_SkillListSect);
            for (const auto& active : m_ActiveSkills) {
                const CSkillInfo* const candidate = active.second;
                if (candidate != nullptr && candidate->m_nSkillId != skillInfo.m_nSkillId &&
                    candidate->m_nReuseDelayGroup == groupId) {
                    selectedSkillId = candidate->m_nSkillId;
                    break;
                }
            }
            return selectedSkillId;
        };

        if (skillInfo.m_ReuseDelayType == SRDT_NORMAL) {
            m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                groupedSkillId(), subjobId, static_cast<std::uint32_t>(reuseDelay), 0
            );
        } else {
            if (groupId > 0) {
                m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                    groupedSkillId(), subjobId, static_cast<std::uint32_t>(reuseDelay), 0
                );
            }
            m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(skillInfo.m_nSkillId, 4, static_cast<std::uint32_t>(reuseDelay), 0);
        }
    }
    if (reuseDelay > 300000) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return reuseDelay;
}

bool CCreature::ConsumeMP(const double amount) {
    // Current L2Server 0x005e843c.
    const double currentMp = m_data->m_dMP;
    if (amount <= currentMp) {
        m_data->SetMP(currentMp - amount);
    }
    m_data->ValidateMP(0.0);
    if (IsPC()) {
        if (User* const user = CastUser(); user != nullptr) {
            user->SendUserMPInfo(0, 0, false);
        }
    }
    return amount <= currentMp;
}

bool CCreature::ConsumeHP(const double amount) {
    // Current L2Server 0x005e84c0.
    const double currentHp = m_data->m_dHP;
    const bool consumed = currentHp > amount;
    if (consumed) {
        m_data->SetHP(std::max(currentHp - amount, 1.0));
    }
    if (IsPC()) {
        if (User* const user = CastUser(); user != nullptr) {
            user->SendUserHPInfo(4, 0, false);
        }
    } else {
        SendStatusUpdate(VCP_HP, 0, 0);
    }
    m_data->m_NeedUpdatePacketBitSet.ClearBit(0xb);
    return consumed;
}

bool CCreature::ConsumePledgeNameValue(const int amount) {
    // Current L2Server 0x005e8568.
    L2_GUARD_FUNCTION();
    if (!IsPC() || amount == 0) {
        return true;
    }
    User* const user = CastUser();
    if (user == nullptr) {
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !pledge->UseNameValue(amount)) {
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->SendSystemMessage_d(SystemMessageId::CLAN_REPUTATION_CONSUMED, amount);
    }
    return true;
}

bool CCreature::CanUseBadSkill(CWorldObject* target, const bool reportFailure) {
    // Current L2Server 0x005ef75c.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }
    if (!IsPCOrPCRelatedNPC() || !target->IsPCOrPCRelatedNPC()) {
        return true;
    }

    CCreature* source = nullptr;
    if (IsSummonOrPet()) {
        source = GetUserOrMaster();
    }

    if (source == nullptr) {
        if (!AreaBasedRule::CastBadSkill(this)) {
            if (reportFailure) {
                SendSystemMessage(SystemMessageId::CANNOT_USE_HOSTILE_SKILLS_HERE);
            }
            return false;
        }

        if (target->IsCreature()) {
            CCreature* const targetCreature = target->CastCreature();
            if (targetCreature != nullptr && !AreaBasedRule::TakeBadSkill(this, targetCreature)) {
                if (reportFailure) {
                    SendSystemMessage(SystemMessageId::CANNOT_USE_HOSTILE_SKILLS_IN_THIS_AREA);
                }
                return false;
            }
        }
        return true;
    }

    if (!AreaBasedRule::CastBadSkill(source)) {
        if (reportFailure) {
            source->SendSystemMessage(SystemMessageId::CANNOT_USE_HOSTILE_SKILLS_HERE);
        }
        return false;
    }

    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (targetCreature != nullptr && !AreaBasedRule::TakeBadSkill(source, targetCreature)) {
            if (reportFailure) {
                source->SendSystemMessage(SystemMessageId::CANNOT_USE_HOSTILE_SKILLS_IN_THIS_AREA);
            }
            return false;
        }
    }
    return true;
}

bool CCreature::IsDoubleCasting(const int skillId, const int level, const int subLevel) {
    L2_GUARD_FUNCTION();

    CCreatureController* const controller = GetController();
    if (controller->GetCurrentActionType() != ACT_SKILL) {
        return false;
    }

    if (!m_bDoubleCastingMod) {
        return false;
    }

    const int currentSubLevel = controller->GetCurrentActionSkillSubLevel();
    const int currentLevel = controller->GetCurrentActionSkillLevel();
    const int currentSkillId = controller->GetCurrentActionSkillId();
    const CSkillInfo* const currentSkill =
        g_SkillDB.GetSkillInfo(CSkillKey(currentSkillId, currentLevel, currentSubLevel), nullptr);
    const CSkillInfo* const newSkill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr);

    if (currentSkill == nullptr || newSkill == nullptr) {
        if (IsPC()) {
            if (newSkill == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][%s] Unregistered new skill("
                //     L"id:%d-level:%d)",
                //     kCreatureServerSourceFile,
                //     0x63b0,
                //     CastUser()->GetName(),
                //     skillId,
                //     level)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[skills] Double casting rejected for %s: requested skill %d, level %d, sublevel %d has no
                //     definition.", CastUser()->GetName(), skillId, level, subLevel)
                // L2_LOG_ERROR(Log,
                //     L"[skills] Double casting rejected for %s: requested skill %d, level %d, sublevel %d has no
                //     definition.", CastUser()->GetName(), skillId, level, subLevel)
                L2_LOG_ERROR(
                    Log,
                    u"[skills] Double casting rejected for %s: requested skill %d, level %d, sublevel %d has no definition.",
                    CastUser()->GetName(), skillId, level, subLevel
                );
            }
            if (currentSkill == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][%s] Unregistered current skill("
                //     L"id:%d-level:%d)",
                //     kCreatureServerSourceFile,
                //     0x63b5,
                //     CastUser()->GetName(),
                //     controller->GetCurrentActionSkillId(),
                //     controller->GetCurrentActionSkillLevel())
                // Log.Add(
                //     LOG_ERROR,
                //     L"[skills] Double casting rejected for %s: current skill lookup %d, level %d, sublevel %d has no
                //     definition. Controller now reports skill %d, level %d.", CastUser()->GetName(), currentSkillId,
                //     currentLevel, currentSubLevel, controller->GetCurrentActionSkillId(),
                //     controller->GetCurrentActionSkillLevel())
                // L2_LOG_ERROR(Log,
                //     L"[skills] Double casting rejected for %s: current skill lookup %d, level %d, sublevel %d has no
                //     definition. Controller now reports skill %d, level %d.", CastUser()->GetName(), currentSkillId,
                //     currentLevel, currentSubLevel, controller->GetCurrentActionSkillId(),
                //     controller->GetCurrentActionSkillLevel())
                L2_LOG_ERROR(
                    Log,
                    u"[skills] Double casting rejected for %s: current skill lookup %d, level %d, sublevel %d has no "
                    u"definition; controller now reports skill %d, level %d.",
                    CastUser()->GetName(), currentSkillId, currentLevel, currentSubLevel,
                    controller->GetCurrentActionSkillId(), controller->GetCurrentActionSkillLevel()
                );
            }
        }
        return false;
    }

    return currentSkill->m_bIsDoubleCasting && newSkill->m_bIsDoubleCasting;
}

void CCreature::CancelCastingSkill(const int magicType, CWorldObject* const target) {
    // Current L2Server 0x0060959c.
    L2_GUARD_FUNCTION();

    CCreatureController* const controller = GetController();
    if (controller->GetCurrentActionType() != ACT_SKILL || controller->GetCurrentActionSkillMagicType() != magicType) {
        return;
    }

    const int skillId = controller->GetCurrentActionSkillId();
    controller->DoNothing(true);

    const int actorId = static_cast<int>(m_nId);
    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, GetPos(), GetBroadcastDist(0xc00), "cd", 0x49, actorId);
    } else if (IsPC()) {
        CastUser()->GetSocket()->Send("cd", 0x49, actorId);
    }

    SendSystemMessage(SystemMessageId::CASTING_INTERRUPTED);
    if (IsPC()) {
        CLogFormatter formatter{};
        formatter.WriteLog2_1(0x195, CastUser(), target, skillId, 0, 0, 0, 0, 0, 0);
    }
}

void CCreature::SendTacticalSignInfo(User& user) {
    L2_GUARD_FUNCTION();
    const int signId = user.GetTacticalSignId(*this);
    if (signId != 0) {
        SendTacticalSignInfo(user, signId);
    }
}

void CCreature::SendTacticalSignInfo(User& user, int signId) {
    L2_GUARD_FUNCTION();
    user.GetSocket()->Send("chdd", 0xfe, 0x100, runtime::Int32FromBits(m_nId), signId);
}

bool CCreature::CanUseMoveToTargetAirShip(CObject* object, bool& canMove) {
    L2_GUARD_FUNCTION();
    if (object == nullptr) {
        canMove = false;
        return false;
    }

    const bool sourceInAirShip = IsInAirShip();
    const bool targetInAirShip = object->IsInAirShip();
    if (!sourceInAirShip) {
        if (targetInAirShip) {
            canMove = false;
        }
        return false;
    }

    if (!targetInAirShip) {
        canMove = false;
    } else {
        const unsigned int targetAirShipId = object->GetMyAirShipID();
        const unsigned int sourceAirShipId = GetMyAirShipID();
        if (sourceAirShipId != targetAirShipId) {
            canMove = false;
        }
    }
    return true;
}

void CCreature::ValidateForceAction(CObject* target, bool& forceAction) {
    L2_GUARD_FUNCTION();

    if (!forceAction) {
        return;
    }
    User* const owner = GetUserOrMaster();
    if (owner == nullptr) {
        return;
    }

    if (!owner->CanForceAttack()) {
        forceAction = false;
        if (!IsPC() || target == nullptr) {
            return;
        }

        User* const targetUser = target->GetUserOrMaster();
        if (targetUser != nullptr && !F2PConfig::CheckPKCount(owner) && !targetUser->m_bEinhasadeTransformed &&
            targetUser->m_data->m_nKarma >= 0 && targetUser->m_data->m_cIsGuilty < 1) {
            const char16_t* const ownerName = owner->GetName();
            owner->GetSocket()->SendSystemMessage_S(
                SystemMessageId::CHARACTER_CANNOT_INITIATE_ATTACKS_ON_NON_HOSTILE_PLAYERS, ownerName
            );
        }
        return;
    }

    if (g_nCountryCode == 3) {
        if (GetSocket() != nullptr && GetSocket()->m_bEconomicRestriction) {
            forceAction = false;
            return;
        }
    }

    bool siegeParticipant = false;
    for (const int castleSiegeId : owner->m_nCastleSiegeId) {
        if (castleSiegeId > 0) {
            siegeParticipant = true;
            break;
        }
    }
    siegeParticipant = siegeParticipant || owner->m_nFortressUnderDefenceSiegeId != 0 ||
        owner->m_nFortressUnderOffenceSiegeId != 0 || owner->m_data->m_nResidenceID != 0;
    if (!siegeParticipant || target == nullptr) {
        return;
    }

    User* const targetUser = target->GetUserOrMaster();
    if (targetUser == nullptr) {
        return;
    }

    const unsigned int relation = owner->GetRelationTo(targetUser);
    int failureReason = 0;
    if ((relation & (1U << 11U)) != 0U) {
        if (targetUser->IsOlympiadPlaying() || targetUser->m_PledgeGameUser.IsPlaying()) {
            return;
        }
        failureReason = 0x86e;
    } else {
        if ((relation & (1U << 19U)) == 0U || targetUser->IsOlympiadPlaying() || targetUser->m_PledgeGameUser.IsPlaying()) {
            return;
        }
        failureReason = 0xac1;
    }

    forceAction = false;
    ActionFailed(failureReason);
}

bool CCreature::ValidateTargetOnAction(CObject* target) {
    L2_GUARD_FUNCTION();
    if (target == this) {
        return true;
    }

    const auto failCurrentAction = [this]() {
        ChangeTarget(nullptr, CTR_DEFAULT);
        if (IsPC()) {
            User* const user = CastUser();
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->SendSystemMessage(SystemMessageId::TARGET_FROZEN);
            }
        }
        return false;
    };

    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (targetCreature->IsFrozen(false)) {
            if (target->IsPC() && (!m_spTarget.IsValid() || m_spTarget.Sid() != target->Id())) {
                User* const targetUser = target->CastUser();
                if (targetUser->m_bIsFreezedByGM || targetUser->m_EventMatchUser.m_bIsPause) {
                    ChangeTarget(target, CTR_DEFAULT);
                    return false;
                }
                if (targetUser->m_DuelUserInfo.IsDuelDead(targetUser) ||
                    (targetUser->m_PledgeGameUser.IsPlayer() && !targetUser->m_PledgeGameUser.IsAlive())) {
                    ChangeTarget(nullptr, CTR_DEFAULT);
                    return false;
                }
            }
            return failCurrentAction();
        }
    }

    if (target->IsNPC()) {
        CNPC* const npc = target->CastNPC();
        if (npc->IsFakeNPC()) {
            npc->KillFakeNpc(*this);
            ChangeTarget(nullptr, CTR_DEFAULT);
            return false;
        }
    }
    return true;
}

void CCreature::Action(CObject* target, bool forceAction, bool suppressMove) {
    L2_GUARD_FUNCTION();

    bool canMove = !suppressMove;
    CObject* currentTarget = m_spTarget.GetObjectW();
    if (runtime::runtime_timing::Matches(this, target)) {
        runtime::runtime_timing::Record(
            "creature_action_enter", this, target,
            "force=%d suppress_move=%d selected_sid=%u "
            "current_action=%d next_action=%d can_act=%d can_attack=%d "
            "stop_mode=%d",
            forceAction ? 1 : 0, suppressMove ? 1 : 0, currentTarget != nullptr ? currentTarget->Id() : 0U,
            static_cast<int>(GetController()->GetCurrentActionType()),
            static_cast<int>(GetController()->GetNextActionType()), m_SkillMod.m_bCanAct ? 1 : 0,
            m_SkillMod.m_bCanAttack ? 1 : 0, m_data->m_StopMode
        );
    }
    ValidateForceAction(currentTarget, forceAction);
    if (!ValidateTargetOnAction(target)) {
        return;
    }

    if (target->IsPC() && CMiniGameAgit::IsPlayer(target->CastUser()) && IsPC() && !CMiniGameAgit::IsPlayer(CastUser())) {
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }

    if (target != this && !target->IsTargetable()) {
        ChangeTarget(nullptr, CTR_DEFAULT);
        return;
    }

    currentTarget = m_spTarget.GetObjectW();
    if (IsPC() && CastUser()->IsAirShipDriver()) {
        CastUser()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return;
    }

    if (target->IsAirShip()) {
        if (target != currentTarget) {
            ChangeTarget(target, CTR_DEFAULT);
        }
        return;
    }

    if (target->IsItem() && IsPC() && CastUser()->m_bFlyMoving) {
        CastUser()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE);
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }

    if (!IsFrozen(true) &&
        ((target->IsItem() && (!IsPC() || CastUser()->m_data->m_nWorldId == g_nWorldId)) ||
            (currentTarget != nullptr && currentTarget == target && target != this))) {
        CWorldObject* const worldTarget = target->CastWorldObject();
        FVector targetPosition{};
        if (worldTarget != nullptr) {
            targetPosition = worldTarget->GetPos();
        } else {
            // Noise: The qualified CObject base-class name does not identify the target. Retain the actual object IDs
            // instead. Log.Add(
            //     LOG_ERROR,
            //     L"Bad Target! %s",
            //     CObject::__GetClassName())
            // Log.Add(
            //     LOG_ERROR,
            //     "[movement] Object %u cannot use target object %u as a world object. Continuing the action with movement
            //     disabled.", m_nId, target->m_nId)
            L2_LOG_ERROR(
                Log,
                "[movement] Object %u cannot use target object %u as a world object; continuing the action with movement "
                "disabled.",
                m_nId, target->m_nId
            );
            canMove = false;
        }

        if (!IsPC() || (m_SkillMod.m_nBlockMoveCount == 0 && m_data->m_bCanMove)) {
            if (IsNPC()) {
                double targetCollisionRadius = 50.0;
                if (target->IsCreature()) {
                    targetCollisionRadius = target->CastCreature()->m_data->m_dCollisionRadius;
                }

                const double deltaX = m_data->m_Pos.x - targetPosition.x;
                const double deltaY = m_data->m_Pos.y - targetPosition.y;
                const double deltaZ = m_data->m_Pos.z - targetPosition.z;
                const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                if (m_SkillMod.m_nBlockMoveCount != 0 || !m_data->m_bCanMove) {
                    canMove =
                        static_cast<double>(m_data->m_nAttackRange) + m_data->m_dCollisionRadius + targetCollisionRadius <=
                            distance &&
                        worldTarget != nullptr && !suppressMove;
                }
            }
        } else {
            canMove = false;
        }

        const bool useRelativeMovement = CanUseMoveToTargetAirShip(target, canMove);
        if (!canMove) {
            if (!forceAction && !target->IsEnemyTo(this)) {
                if (!IsPC()) {
                    return;
                }
                if (!target->IsNPC()) {
                    // ActionFailed(0)
                    ActionFailed(AF_DEFAULT);
                    return;
                }
                GetController()->Talk(target->CastCreature());
                return;
            }

            GetController()->Attack(target, true);
            return;
        }

        if (IsPC()) {
            const double deltaX = m_data->m_Pos.x - targetPosition.x;
            const double deltaY = m_data->m_Pos.y - targetPosition.y;
            if (16777216.0 < deltaX * deltaX + deltaY * deltaY) {
                // ActionFailed(0)
                ActionFailed(AF_DEFAULT);
                return;
            }
        }

        const SkillUseInfo skillUseInfo;
        if (useRelativeMovement) {
            GetController()->MoveToTargetRelative(target, forceAction, skillUseInfo, 0);
        } else {
            GetController()->MoveToTarget(target, forceAction, skillUseInfo, 0);
        }
        return;
    }

    if (!m_spTarget.IsValid() || m_spTarget.Sid() != target->Id()) {
        ChangeTarget(target, CTR_DEFAULT);
        return;
    }
    // ActionFailed(0)
    ActionFailed(AF_DEFAULT);
}

bool CCreature::SkillAction(
    int skillId, bool forceAttack, bool stand, CItem* item, const CSkillInfo* skillInfo, int type, int parameter
) {
    // Current L2Server 0x00629a5c.
    L2_GUARD_FUNCTION();
    // Portable opt-in diagnostic (off).
    const bool magicTrace = std::getenv("L2_MAGIC_TRACE") != nullptr && IsPC();
    if (magicTrace) {
        // std::fprintf(
        //     stderr,
        //     "[MAGIC_TRACE] skill-action-enter sid=%u dbid=%d skill=%d "
        //     "target=%u force=%d stand=%d item=%p supplied-info=%p "
        //     "active=%d alive=%d can-act=%d can-move=%d\n",
        //     Id(),
        //     static_cast<int>(GetDBID()),
        //     skillId,
        //     m_spTarget.Sid(),
        //     forceAttack ? 1 : 0,
        //     stand ? 1 : 0,
        //     static_cast<void*>(item),
        //     static_cast<const void*>(skillInfo),
        //     m_data->m_bIsActive ? 1 : 0,
        //     m_data->m_bIsAlive != 0 ? 1 : 0,
        //     m_SkillMod.m_bCanAct ? 1 : 0,
        //     m_data->m_bCanMove ? 1 : 0)
        // Noise: Raw addresses add no skill-state explanation. Report presence instead.
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Info,
        //     "[magic trace] Entering skill action for object %u (DB ID %u), skill %d, target %u. Force attack: %s, stand:
        //     %s, item supplied: %s, skill definition supplied: %s. Active: %s, alive: %s, can act: %s, can move: %s.",
        //     Id(),
        //     GetDBID(),
        //     skillId,
        //     m_spTarget.Sid(),
        //     forceAttack ? "yes" : "no",
        //     stand ? "yes" : "no",
        //     item != nullptr ? "yes" : "no",
        //     skillInfo != nullptr ? "yes" : "no",
        //     m_data->m_bIsActive ? "yes" : "no",
        //     m_data->m_bIsAlive != 0 ? "yes" : "no",
        //     m_SkillMod.m_bCanAct ? "yes" : "no",
        //     m_data->m_bCanMove ? "yes" : "no")
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Info,
            "[magic trace] Entering skill action for object %u (DB ID %u), skill %d, target %u; force attack: %s, stand: "
            "%s, item supplied: %s, skill definition supplied: %s; active: %s, alive: %s, can act: %s, can move: %s.",
            Id(), GetDBID(), skillId, m_spTarget.Sid(), forceAttack ? "yes" : "no", stand ? "yes" : "no",
            item != nullptr ? "yes" : "no", skillInfo != nullptr ? "yes" : "no", m_data->m_bIsActive ? "yes" : "no",
            m_data->m_bIsAlive != 0 ? "yes" : "no", m_SkillMod.m_bCanAct ? "yes" : "no", m_data->m_bCanMove ? "yes" : "no"
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
    }

    LastAction& lastAction = g_LastAction[static_cast<std::size_t>(CThreadManager::GetThreadID())];
    lastAction.m_SkillID = skillId;

    if (m_FollowData.m_FollowType != 3) {
        m_FollowData.m_FollowType = 0;
        m_FollowData.m_FollowDistance = -1;
        m_FollowData.m_FollowAngle = 0;
        m_FollowData.m_FollowOffsetX = 0;
        m_FollowData.m_FollowOffsetY = 0;
    }

    if (!IsPC()) {
        std::size_t index = 0;
        while (index < 24 && m_data->m_sName[index] != u'\0') {
            lastAction.m_Name[index] = m_data->m_sName[index];
            ++index;
        }
        lastAction.m_Name[index] = u'\0';
        lastAction.m_PlayerClassID = static_cast<std::int32_t>(m_data->m_nClass);
    } else {
        User* const user = CastUser();
        if (user->GetPrivateStoreType() != 0 && skillId == 0x641) {
            SendSystemMessage(SystemMessageId::CLOSE_PRIVATE_STORE_OR_WORKSHOP_SETUP_WINDOW);
            if (magicTrace) {
                // std::fprintf(
                //     stderr,
                //     "[MAGIC_TRACE] skill-action-reject sid=%u skill=%d "
                //     "reason=private-store\n",
                //     Id(),
                //     skillId)
                // runtime::logging::WriteDirect(
                //     stderr, "l2server", runtime::logging::Level::Info,
                //     "[magic trace] Rejected skill action for object %u, skill %d: private-store or workshop setup is
                //     open.", Id(), skillId)
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Info,
                    "[magic trace] Rejected skill action for object %u, skill %d: private-store or workshop setup is open.",
                    Id(), skillId
                );
                // std::fflush(stderr), // WriteDirect flushes the complete record.
            }
            return false;
        }
        if (user->m_bFlyMoving) {
            SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE);
            if (magicTrace) {
                // std::fprintf(
                //     stderr,
                //     "[MAGIC_TRACE] skill-action-reject sid=%u skill=%d "
                //     "reason=fly-moving\n",
                //     Id(),
                //     skillId)
                // runtime::logging::WriteDirect(
                //     stderr, "l2server", runtime::logging::Level::Info,
                //     "[magic trace] Rejected skill action for object %u, skill %d: Sayune movement is active.",
                //     Id(),
                //     skillId)
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Info,
                    "[magic trace] Rejected skill action for object %u, skill %d: Sayune movement is active.", Id(), skillId
                );
                // std::fflush(stderr), // WriteDirect flushes the complete record.
            }
            return false;
        }
        lastAction.m_Name[0] = u'\0';
        lastAction.m_PlayerClassID = static_cast<std::int32_t>(m_data->m_nClass);
    }

    if (item != nullptr) {
        lastAction.m_ItemClassID = item->GetClassId();
    }

    ValidateForceAction(m_spTarget.GetObjectW(), forceAttack);
    if (skillInfo == nullptr) {
        if (item == nullptr) {
            if (IsLockedSkill(skillId) == 1) {
                if (magicTrace) {
                    // std::fprintf(
                    //     stderr,
                    //     "[MAGIC_TRACE] skill-action-reject sid=%u "
                    //     "skill=%d reason=locked-skill\n",
                    //     Id(),
                    //     skillId)
                    // runtime::logging::WriteDirect(
                    //     stderr, "l2server", runtime::logging::Level::Info,
                    //     "[magic trace] Rejected skill action for object %u, skill %d: the skill is locked.",
                    //     Id(),
                    //     skillId)
                    L2_LOG_DIRECT(
                        stderr, runtime::logging::Level::Info,
                        "[magic trace] Rejected skill action for object %u, skill %d: the skill is locked.", Id(), skillId
                    );
                    // std::fflush(stderr), // WriteDirect flushes the complete record.
                }
                return false;
            }
            skillInfo = GetUsableSkill(skillId);
            if (skillInfo != nullptr && skillInfo->HasAlterSkills()) {
                skillId = GetAlterSkill(&skillInfo);
            }
        } else {
            skillInfo = static_cast<const CSkillInfo*>(item->m_const->m_pSkill);
        }

        if (skillInfo == nullptr) {
            if (std::getenv("L2_MAGIC_TRACE") != nullptr) {
                int acquiredLevel = -1;
                int acquiredSubLevel = -1;
                std::size_t acquiredCount = 0;
                {
                    L2_GUARD(m_AcquiredSkillsSect);
                    acquiredCount = m_AcquiredSkills.size();
                    const auto acquired = m_AcquiredSkills.find(skillId);
                    if (acquired != m_AcquiredSkills.end()) {
                        acquiredLevel = acquired->second.m_nLevel;
                        acquiredSubLevel = acquired->second.m_nSubLevel;
                    }
                }

                bool active = false;
                bool aura = false;
                bool toggle = false;
                std::size_t activeCount = 0;
                std::size_t auraCount = 0;
                std::size_t toggleCount = 0;
                {
                    L2_GUARD(m_SkillListSect);
                    active = m_ActiveSkills.find(skillId) != m_ActiveSkills.end();
                    aura = m_AuraSkills.find(skillId) != m_AuraSkills.end();
                    toggle = m_ToggleSkills.find(skillId) != m_ToggleSkills.end();
                    activeCount = m_ActiveSkills.size();
                    auraCount = m_AuraSkills.size();
                    toggleCount = m_ToggleSkills.size();
                }

                // std::fprintf(
                //     stderr,
                //     "[MAGIC_TRACE] unusable sid=%u skill=%d "
                //     "class=%d level=%d acquired=%d/%d "
                //     "active=%d aura=%d toggle=%d counts=%zu/%zu/%zu/%zu\n",
                //     m_nId,
                //     skillId,
                //     static_cast<int>(m_data->m_nClass),
                //     m_data->m_nLevel,
                //     acquiredLevel,
                //     acquiredSubLevel,
                //     active,
                //     aura,
                //     toggle,
                //     acquiredCount,
                //     activeCount,
                //     auraCount,
                //     toggleCount)
                // runtime::logging::WriteDirect(
                //     stderr, "l2server", runtime::logging::Level::Info,
                //     "[magic trace] No usable skill definition for object %u, skill %d, class %d, level %d. Acquired level
                //     %d, sublevel %d. Present in active list: %s, aura list: %s, toggle list: %s. List sizes: acquired %zu,
                //     active %zu, aura %zu, toggle %zu. Rejecting the skill action.", m_nId, skillId,
                //     static_cast<int>(m_data->m_nClass),
                //     m_data->m_nLevel,
                //     acquiredLevel,
                //     acquiredSubLevel,
                //     active ? "yes" : "no",
                //     aura ? "yes" : "no",
                //     toggle ? "yes" : "no",
                //     acquiredCount,
                //     activeCount,
                //     auraCount,
                //     toggleCount)
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Info,
                    "[magic trace] No usable skill definition for object %u, skill %d, class %d, level %d; acquired level "
                    "%d, sublevel %d; present in active list: %s, aura list: %s, toggle list: %s; list sizes: acquired %zu, "
                    "active %zu, aura %zu, toggle %zu. Rejecting the skill action.",
                    m_nId, skillId, static_cast<int>(m_data->m_nClass), m_data->m_nLevel, acquiredLevel, acquiredSubLevel,
                    active ? "yes" : "no", aura ? "yes" : "no", toggle ? "yes" : "no", acquiredCount, activeCount, auraCount,
                    toggleCount
                );
                // std::fflush(stderr), // WriteDirect flushes the complete record.
            }
            if (IsPC() && static_cast<std::uint32_t>(skillId - 0x61e) < 4U) {
                User* const user = CastUser();
                if (user->IsTransformed(TransformType::Max) != nullptr) {
                    user->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLASS_WHILE_TRANSFORMED);
                }
            }
            return false;
        }
    }

    const auto traceRoute = [&](const char* const route) {
        if (!magicTrace) {
            return;
        }
        // std::fprintf(
        //     stderr,
        //     "[MAGIC_TRACE] skill-action-route sid=%u skill=%d/%d/%d "
        //     "target=%u target-type=%d operate=%d cast-range=%d "
        //     "stand=%d route=%s\n",
        //     Id(),
        //     skillInfo->m_nSkillId,
        //     skillInfo->m_nLevel,
        //     skillInfo->m_nSubLevel,
        //     m_spTarget.Sid(),
        //     static_cast<int>(skillInfo->m_TargetType),
        //     static_cast<int>(skillInfo->m_OperateType),
        //     skillInfo->m_nCastRange,
        //     stand ? 1 : 0,
        //     route)
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Info,
        //     "[magic trace] Selected route for object %u, skill %d, level %d, sublevel %d, target %u: %s. Target type %d,
        //     operation type %d, cast range %d, stand: %s.", Id(), skillInfo->m_nSkillId, skillInfo->m_nLevel,
        //     skillInfo->m_nSubLevel,
        //     m_spTarget.Sid(),
        //     route,
        //     static_cast<int>(skillInfo->m_TargetType),
        //     static_cast<int>(skillInfo->m_OperateType),
        //     skillInfo->m_nCastRange,
        //     stand ? "yes" : "no")
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Info,
            "[magic trace] Selected route for object %u, skill %d, level %d, sublevel %d, target %u: %s; target type %d, "
            "operation type %d, cast range %d, stand: %s.",
            Id(), skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, m_spTarget.Sid(), route,
            static_cast<int>(skillInfo->m_TargetType), static_cast<int>(skillInfo->m_OperateType), skillInfo->m_nCastRange,
            stand ? "yes" : "no"
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
    };

    if (m_SkillMod.m_nBlockMoveCount != 0 || !m_data->m_bCanMove) {
        stand = true;
    }

    int selectedSkillId = skillId;
    if ((!m_bDoubleCastingMod || skillInfo->m_nAttachDoubleCastSkillId < 1) && skillInfo->m_nAttachToggleGroupId > 0) {
        const int attachedSkillId = skillInfo->GetAttachSkillId(GetOnGroupToggleSkillId(skillInfo->m_nAttachToggleGroupId));
        if (attachedSkillId > 0) {
            selectedSkillId = attachedSkillId;
        }
    }
    lastAction.m_AlteredSkillID = selectedSkillId;

    std::int32_t usedItemSID = 0;
    if (item != nullptr) {
        const std::uint32_t itemSID = item->Id();
        std::memcpy(&usedItemSID, &itemSID, sizeof(usedItemSID));
    }

    SkillUseInfo skillUseInfo;
    skillUseInfo.m_skillID = selectedSkillId;
    skillUseInfo.m_skillLevel = skillInfo->m_nLevel;
    skillUseInfo.m_skillSubLevel = skillInfo->m_nSubLevel;
    skillUseInfo.m_castRange = skillInfo->m_nCastRange;
    skillUseInfo.m_type = type;
    skillUseInfo.m_param1 = parameter;

    CObject* target = m_spTarget.GetObjectW();
    const bool relativeMovement = CanUseMoveToTargetAirShip(target, stand);
    const SkillOperateType operateType = skillInfo->m_OperateType;
    if (stand || m_data->m_StopMode == STP_SIT || skillInfo->m_TargetType == STGT_SELF || operateType == SKILL_TOGGLE ||
        operateType == SKILL_AURA || operateType == SKILL_GROUP_TOGGLE) {
        // traceRoute("controller-skill-direct")
        traceRoute("direct controller skill call");
        GetController()->Skill(skillUseInfo, forceAttack, stand, static_cast<unsigned int>(usedItemSID));
        lastAction.m_SkillID = 0;
        lastAction.m_ItemClassID = 0;
        return true;
    }

    if (skillInfo->m_TargetType == STGT_MY_MENTOR && IsPC()) {
        User* const user = CastUser();
        if (user->IsAlreadyHaveMentor()) {
            CSPointer<User> mentor = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentWorldId(), CastUser()->m_Mentee.m_Mentor.nCharDBId)
            );
            if (mentor) {
                m_spTarget = mentor->CastCreature()->GetSmartID();
            }
        }
    }

    const SkillTargetTypeEnum targetType = skillInfo->m_TargetType;
    if (targetType == STGT_SUMMON) {
        // traceRoute("summon-target")
        traceRoute("summon target handling");
        if (!IsPC()) {
            return true;
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(CastUser(), summon, {
            if (relativeMovement) {
                GetController()->MoveToTargetRelative(summon, forceAttack, skillUseInfo, usedItemSID);
            } else {
                const FVector source = GetPos();
                const FVector destination = summon->GetPos();
                const double acceptableDistance = std::max(static_cast<double>(skillInfo->m_nCastRange) * 0.8, 0.0);
                const double deltaX = source.x - destination.x;
                const double deltaY = source.y - destination.y;
                const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                if (acceptableDistance <= distance) {
                    GetController()->MoveToTarget(summon, forceAttack, skillUseInfo, usedItemSID);
                } else {
                    GetController()->Skill(skillUseInfo, forceAttack, false, static_cast<unsigned int>(usedItemSID));
                }
            }
            break;
        });
        return true;
    }

    if (targetType == STGT_GROUND) {
        // traceRoute("ground-target")
        traceRoute("ground target handling");
        if (m_NonTargetSkillPos.IsEqual(FVector{})) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"m_NonTargetSkillPos is not valid, skillId[%d] type[%d] name[%s]",
            //     selectedSkillId,
            //     static_cast<int>(targetType),
            //     GetName())
            // Log.Add(
            //     LOG_WARN,
            //     L"[skills] Ground-target skill %d (target type %d) rejected for %s: the target position is unset.",
            //     selectedSkillId, static_cast<int>(targetType), GetName())
            // L2_LOG_WARN(Log,
            //     L"[skills] Ground-target skill %d (target type %d) rejected for %s: the target position is unset.",
            //     selectedSkillId, static_cast<int>(targetType), GetName())
            L2_LOG_WARN(
                Log, u"[skills] Ground-target skill %d (target type %d) rejected for %s: the target position is unset.",
                selectedSkillId, static_cast<int>(targetType), GetName()
            );
            return false;
        }

        m_NonTargetSkillPos.z += 40.0;
        const int ground = CWorld::Inst().GetGeo()->FindGround(
            runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.x),
            runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.y),
            runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.z)
        );
        if (ground == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"m_NonTargetSkillPos is not valid. [%d][%d][%d]",
            //     runtime::Win64CvttDoubleToInt32(
            //         m_NonTargetSkillPos.x),
            //     runtime::Win64CvttDoubleToInt32(
            //         m_NonTargetSkillPos.y),
            //     runtime::Win64CvttDoubleToInt32(
            //         m_NonTargetSkillPos.z))
            // Log.Add(
            //     LOG_WARN,
            //     "[skills] Ground-target skill %d rejected: FindGround returned 0 at adjusted position (%d, %d, %d).",
            //     selectedSkillId, runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.x),
            //     runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.y),
            //     runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.z))
            L2_LOG_WARN(
                Log, "[skills] Ground-target skill %d rejected: FindGround returned 0 at adjusted position (%d, %d, %d).",
                selectedSkillId, runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.x),
                runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.y),
                runtime::Win64CvttDoubleToInt32(m_NonTargetSkillPos.z)
            );
            return false;
        }
        m_NonTargetSkillPos.z = static_cast<double>(ground);

        const FVector source = GetPos();
        const double acceptableDistance = std::max(static_cast<double>(skillInfo->m_nCastRange) * 0.8, 0.0);
        const double deltaX = source.x - m_NonTargetSkillPos.x;
        const double deltaY = source.y - m_NonTargetSkillPos.y;
        const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (acceptableDistance <= distance) {
            const FVector direction{
                m_NonTargetSkillPos.x - source.x, m_NonTargetSkillPos.y - source.y, m_NonTargetSkillPos.z - source.z};
            const FVector normal = direction.SafeNormal();
            const double travel = distance - acceptableDistance;
            const FVector destination{
                source.x + normal.x * travel, source.y + normal.y * travel, source.z + normal.z * travel};
            CCreatureController::OptionData optionData(selectedSkillId, skillInfo->m_nLevel, forceAttack, 0, 0);
            GetController()->Move(destination, true, &optionData);
            return true;
        }

        GetController()->Skill(skillUseInfo, forceAttack, false, static_cast<unsigned int>(usedItemSID));
        return true;
    }

    if (targetType == STGT_ARTILLERY || targetType == STGT_SHIP_CANNON) {
        // traceRoute("artillery-controller-skill")
        traceRoute("artillery controller skill call");
        GetController()->Skill(skillUseInfo, forceAttack, false, static_cast<unsigned int>(usedItemSID));
        return true;
    }

    if (Id() == m_spTarget.GetId()) {
        // traceRoute("self-controller-skill")
        traceRoute("self-target controller skill call");
        GetController()->Skill(skillUseInfo, forceAttack, stand, static_cast<unsigned int>(usedItemSID));
        lastAction.m_SkillID = 0;
        lastAction.m_ItemClassID = 0;
        return true;
    }

    if (!IsPC() || !m_data->IsFlying()) {
        target = m_spTarget.GetObjectW();
        if (relativeMovement) {
            // traceRoute("move-to-target-relative")
            traceRoute("movement toward the target in relative coordinates");
            GetController()->MoveToTargetRelative(target, forceAttack, skillUseInfo, usedItemSID);
        } else {
            // traceRoute("move-to-target")
            traceRoute("movement toward the target");
            GetController()->MoveToTarget(target, forceAttack, skillUseInfo, usedItemSID);
        }
        return true;
    }

    StopMove();
    GetController()->ActionBroadcastStopMove();
    // traceRoute("flying-controller-skill")
    traceRoute("flying controller skill call");
    GetController()->Skill(skillUseInfo, forceAttack, false, static_cast<unsigned int>(usedItemSID));
    return true;
}

void CCreatureController::Move(FVector destination, bool pathFind, OptionData* optionData) {
    CCreature* const actor = m_spActor.GetCreature();
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_serializer_enqueue", actor, nullptr,
            "destination=%.3f,%.3f,%.3f pathfind=%d option=%d "
            "current_action=%d next_action=%d",
            destination.x, destination.y, destination.z, pathFind ? 1 : 0, optionData != nullptr ? 1 : 0,
            static_cast<int>(GetCurrentActionType()), static_cast<int>(GetNextActionType())
        );
    }
    __async_arg_Move argument(destination, pathFind, optionData);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Move(pad); }, &argument, L"Move"
    );
}

void CCreatureController::MoveToTarget(CObject* target, bool forceAttack, SkillUseInfo skillUseInfo, int usedItemSID) {
    __async_arg_MoveToTarget argument(target, forceAttack, skillUseInfo, usedItemSID);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_MoveToTarget(pad); }, &argument,
        L"MoveToTarget"
    );
}

void CCreatureController::MoveToTargetRelative(
    CObject* target, bool forceAttack, SkillUseInfo skillUseInfo, int usedItemSID
) {
    __async_arg_MoveToTargetRelative argument(target, forceAttack, skillUseInfo, usedItemSID);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_MoveToTargetRelative(pad); },
        &argument, L"MoveToTargetRelative"
    );
}

void CCreatureController::Skill(SkillUseInfo skillUseInfo, bool forceAttack, bool flag, unsigned int skillIndex) {
    // Current L2Server 0x00529e9c.
    CCreature* const actor = m_spActor.GetCreature();
    if (std::getenv("L2_MAGIC_TRACE") != nullptr && actor != nullptr && actor->IsPC()) {
        // std::fprintf(
        //     stderr,
        //     "[MAGIC_TRACE] controller-enqueue sid=%u dbid=%d "
        //     "skill=%d/%d/%d target=%u force=%d surely=%d item=%u "
        //     "current-action=%d next-action=%d\n",
        //     actor->Id(),
        //     static_cast<int>(actor->GetDBID()),
        //     skillUseInfo.m_skillID,
        //     skillUseInfo.m_skillLevel,
        //     skillUseInfo.m_skillSubLevel,
        //     actor->m_spTarget.Sid(),
        //     forceAttack ? 1 : 0,
        //     flag ? 1 : 0,
        //     skillIndex,
        //     static_cast<int>(GetCurrentActionType()),
        //     static_cast<int>(GetNextActionType()))
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Info,
        //     "[magic trace] Preparing controller work for object %u (DB ID %u), skill %d, level %d, sublevel %d, target %u.
        //     force attack: %s, controller flag: %s, used item object ID %u. Current action %d, next action %d.",
        //     actor->Id(),
        //     actor->GetDBID(),
        //     skillUseInfo.m_skillID,
        //     skillUseInfo.m_skillLevel,
        //     skillUseInfo.m_skillSubLevel,
        //     actor->m_spTarget.Sid(),
        //     forceAttack ? "yes" : "no",
        //     flag ? "yes" : "no",
        //     skillIndex,
        //     static_cast<int>(GetCurrentActionType()),
        //     static_cast<int>(GetNextActionType()))
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Info,
            "[magic trace] Preparing controller work for object %u (DB ID %u), skill %d, level %d, sublevel %d, target %u; "
            "force attack: %s, controller flag: %s, used item object ID %u; current action %d, next action %d.",
            actor->Id(), actor->GetDBID(), skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel,
            actor->m_spTarget.Sid(), forceAttack ? "yes" : "no", flag ? "yes" : "no", skillIndex,
            static_cast<int>(GetCurrentActionType()), static_cast<int>(GetNextActionType())
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
    }
    __async_arg_Skill argument(skillUseInfo, forceAttack, flag, skillIndex);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Skill(pad); }, &argument, L"Skill"
    );
}

void CCreatureController::Attack(CObject* target, bool surely) {
    CCreature* const actor = m_spActor.GetCreature();
    if (runtime::runtime_timing::Matches(actor, target)) {
        runtime::runtime_timing::Record(
            "attack_serializer_enqueue", actor, target, "surely=%d current_action=%d next_action=%d", surely ? 1 : 0,
            static_cast<int>(GetCurrentActionType()), static_cast<int>(GetNextActionType())
        );
    }
    __async_arg_Attack argument(target, surely);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Attack(pad); }, &argument,
        L"Attack"
    );
}

void CCreatureController::MoveRelatively(CAbstractCarrier* carrier, FVector relativePosition) {
    __async_arg_MoveRelatively argument(carrier, relativePosition);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_MoveRelatively(pad); }, &argument,
        L"MoveRelatively"
    );
}

void CCreature::StopMove() {
    L2_GUARD_FUNCTION();
    GetController()->OnStopMove();
}

void CCreature::MoveToLocation(int x, int y, int z, PathFindType pathFindType) {
    L2_GUARD_FUNCTION();
    GetController()->Move(
        FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)), pathFindType == UsePathFind, nullptr
    );
}

void CCreature::MoveToLocationRelative(CAbstractCarrier* carrier, int x, int y, int z) {
    L2_GUARD_FUNCTION();
    GetController()->MoveRelatively(
        carrier, FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z))
    );
}

std::int64_t CCreature::CalcResurrectExp(const double resurrectExpRatio, bool) {
    L2_GUARD_FUNCTION();
    double experience = 0.0;
    double percent = resurrectExpRatio;

    if (IsPC() || (IsNPC() && CastNPC()->IsPet())) {
        experience = static_cast<double>(m_nDownExp) * -1.0;
        if (!(resurrectExpRatio > 1.0)) {
            experience *= resurrectExpRatio;
            percent = resurrectExpRatio * 100.0;
        } else {
            percent = 100.0;
        }

        if (experience < 0.0) {
            experience = 0.0;
        }
        if (percent < 0.0) {
            percent = 0.0;
        }
        if (experience <= 0.0) {
            percent = 0.0;
        }
    } else {
        percent = 0.0;
    }

    m_data->m_nResurrectRatio = runtime::Win64CvttDoubleToInt32(percent);
    return runtime::Win64CvttDoubleToInt64(experience);
}

void CCreature::SetLevel(unsigned int level) {
    L2_GUARD_FUNCTION();
    const unsigned int boundedLevel = std::min(level, 120U);
    L2_GUARD(*m_lock);
    SetExp(ExpByLevel[boundedLevel - 1U]);
}

void CCreature::ChangeRace(const RaceType race, const int fighterMagicType) {
    // Current L2Server 0x005f0ca0.
    L2_GUARD_FUNCTION();
    User* const user = CastUser();
    if (user == nullptr) {
        return;
    }

    const auto applyClass = [this, user, race](const int classType, const int) {
        L2_GUARD(*m_lock);
        m_data->m_nSubJob0Class = classType;
        m_data->SetClass(static_cast<ClassType>(classType));
        user->m_nJobGroup = static_cast<int>(CT_to_FMT[static_cast<int>(m_data->m_nClass)]);
        db.RequestSaveUserJobGroup(*user);
        user->m_nBaseClass = CPCParamDB::GetBasicClassByRaceAndFTM(race, user->m_nJobGroup == 0, m_data->m_Sex == ST_MALE);
    };

    switch (race) {
        case RT_HUMAN:
            applyClass(fighterMagicType == 0 ? 0 : 10, fighterMagicType == 0 ? 0x6463 : 0x6459);
            break;
        case RT_ELF:
            applyClass(fighterMagicType == 0 ? 0x12 : 0x19, fighterMagicType == 0 ? 0x6479 : 0x646f);
            break;
        case RT_DARKELF:
            applyClass(fighterMagicType == 0 ? 0x1f : 0x26, fighterMagicType == 0 ? 0x648f : 0x6485);
            break;
        case RT_ORC:
            applyClass(fighterMagicType == 0 ? 0x2c : 0x31, fighterMagicType == 0 ? 0x64a5 : 0x649b);
            break;
        case RT_DWARF:
            applyClass(0x35, 0x64b8);
            break;
        case RT_KAMAEL:
            applyClass(m_data->m_Sex == ST_MALE ? 0x7b : 0x7c, m_data->m_Sex == ST_MALE ? 0x64ce : 0x64c4);
            break;
        case RT_ERTHEIA:
            if (fighterMagicType < 2) {
                applyClass(fighterMagicType + 0xb6, 0x64e1);
            } else {
                user->GetSocket()->SendSystemMessage(u"SYS", u" : 0:FIGHTER , 1:MAGE");
            }
            break;
        default:
            break;
    }

    L2_GUARD(*m_lock);
    m_data->SetRace(race);
}

int CCreature::UltimateSkillGaugeInc(int value) {
    // Current L2Server 0x005e7f64.
    L2_GUARD_FUNCTION();
    if (!IsPC()) {
        return 0;
    }

    User* const user = CastUser();
    if (user == nullptr ||
        (!User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass())) &&
            !User::IsInSixthClassGroup(static_cast<ClassType>(user->GetClass())))) {
        return 0;
    }

    if (value > 150) {
        value = 150;
    }
    L2_GUARD(*m_lock);
    m_data->UltimateSkillGauge(m_data->m_UltimateSkillGauge + value);

    for (int level = 0; level < 3; ++level) {
        if (kUltimateSkillLevelGauge[level] < m_data->m_UltimateSkillGauge &&
            m_data->m_UltimateSkillGauge < kUltimateSkillLevelGauge[level + 1]) {
            if (m_data->m_UltimateSkillLv != level) {
                if (IsPC()) {
                    ParamChanged(std::int64_t{PCB_CHAR_SKILL});
                } else {
                    ParamChanged();
                }
                m_data->m_UltimateSkillLv = level;
            }
            return m_data->m_UltimateSkillGauge;
        }
    }
    return 0;
}

std::int64_t CCreature::GetResurrectExpPremium(CCreature* creature, double resurrectExpRatio) {
    L2_GUARD_FUNCTION();
    const double ratio = GetResurrectExpRatio(resurrectExpRatio, creature->WitBonusF());
    return CalcResurrectExp(ratio, true);
}

std::int64_t CCreature::GetResurrectExp(CCreature* creature, double resurrectExpRatio) {
    L2_GUARD_FUNCTION();
    const double ratio = GetResurrectExpRatio(resurrectExpRatio, creature->WitBonusF());
    return CalcResurrectExp(ratio, false);
}

const double CCreature::m_ParamMinValue[VALIDATE::MAX] = {
    10.0,
    10.0,
    1.0,
    1.0,
    0.5,
    0.5,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0,
};

int CCreature::Level() const { return m_data->m_nLevel; }

bool CCreature::IsVisible() { return m_data->m_StatusFlag[2] && !m_data->m_bGMInvisible; }

bool CCreature::SetVisible(bool visible) {
    // Current L2Server 0x00609a88.
    L2_GUARD_FUNCTION();
    if (m_data->m_StatusFlag[2] == visible) {
        return false;
    }

    m_data->m_StatusFlag[2] = visible;
    CContainer* const container = CContainerDB::Inst().GetContainer(GetContainerIndex());
    CContainerIndex neighbors;
    if (container != nullptr) {
        neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);
    }

    if (!visible) {
        if (container != nullptr) {
            class ContainerFilter final : public CContainerFilter {
            public:
                explicit ContainerFilter(const std::int32_t subjectId) : m_SubjectId(subjectId) {}

                bool CanDo(CObject* object) override { return static_cast<std::int32_t>(object->m_nId) != m_SubjectId; }

            private:
                std::int32_t m_SubjectId;
            };

            class ContainerFunc final : public CContainerFunc {
            public:
                explicit ContainerFunc(CCreature* subject) : m_Subject(subject) {}

                void Do(CObject* object) override {
                    CCreature* const observer = object->IsCreature() ? object->CastCreature() : nullptr;
                    if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                        if (observer != nullptr && m_Subject->CanBeSeenBy(*observer)) {
                            return;
                        }
                        object->OutOfSight(m_Subject, false);
                    }
                }

            private:
                CCreature* m_Subject;
            };


            ContainerFilter filter(static_cast<std::int32_t>(m_nId));
            ContainerFunc function(this);
            FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
        }
        ResetAllTacticalSignToMeExcludeMyParty();
    } else {
        if (IsPC()) {
            User* const user = CastUser();
            CPledgeGameField* const field = user != nullptr ? user->m_PledgeGameUser.GetField() : nullptr;
            if (field != nullptr && field->GetStatus() == PledgeGameFieldStatus::WAIT_MATCH) {
                return false;
            }
        }

        if (IsActive() && container != nullptr) {
            class ContainerFilter final : public CContainerFilter {
            public:
                explicit ContainerFilter(const std::int32_t subjectId) : m_SubjectId(subjectId) {}

                bool CanDo(CObject* object) override { return static_cast<std::int32_t>(object->m_nId) != m_SubjectId; }

            private:
                std::int32_t m_SubjectId;
            };

            class ContainerFunc final : public CContainerFunc {
            public:
                explicit ContainerFunc(CCreature* subject) : m_Subject(subject) {}

                void Do(CObject* object) override {
                    if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                        object->InSight(m_Subject, false);
                    }
                }

            private:
                CCreature* m_Subject;
            };


            ContainerFilter filter(static_cast<std::int32_t>(m_nId));
            ContainerFunc function(this);
            FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);
        } else {
            if (GetClassId() == 0xf9c25 || GetClassId() == 0xf9c26 || GetClassId() == 0xf9c27 || GetClassId() == 0xf9c28) {
                // Noise: The issue-specific class-ID trace adds no visibility outcome. The class-selection conditions and
                // visibility transitions remain unchanged. Log.Add(
                //     LOG_ERROR,
                //     "TTP54533 ClassId[%d]",
                //     GetClassId())
            }
        }
    }

    // The current body calls this virtual condition and ignores
    // its result. It does not dispatch ParamChanged here.
    IsNPC();
    return true;
}

bool CCreature::SetVisibleBySkill(bool visible, bool broadcast) {
    // Current L2Server 0x00622804.
    L2_GUARD_FUNCTION();
    if (IsVisible() != !visible || m_SkillDurationMod.m_bHide != visible) {
        return false;
    }

    m_SkillDurationMod.m_bHide = !visible;
    if (IsPC()) {
        if (broadcast && visible) {
            bool deleted = false;
            {
                L2_GUARD(m_InfectedSkillsSect);
                deleted = _DeleteAbnormalStatus(ABNS_HIDE, this, nullptr, true, SPET_ETC, false);
            }
            if (deleted) {
                if (!IsPC()) {
                    ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                } else {
                    CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
                }
            }
        }

        if (!SetVisible(visible)) {
            return false;
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(CastUser(), summon, {
            // Current 0x00622804 reads shared-data offset 0x90, which is
            // layout-asserted as m_Race. M_nClass is at offset 0x94.
            if (static_cast<int>(summon->m_data->m_Race) != 0x18) {
                summon->SetVisibleBySkill(visible, true);
            }
        });
        return true;
    }

    if (!m_data->IsSummonOrPet()) {
        return false;
    }

    CSummon* const summon = CastSummon();
    if (summon == nullptr ||
        // Current 0x00622804 again reads m_Race at offset 0x90.
        static_cast<int>(summon->m_data->m_Race) == 0x18) {
        return false;
    }

    CNPC* const npc = CastNPC();
    User* const master = npc != nullptr ? npc->GetMaster() : nullptr;
    if (master != nullptr) {
        master->SetVisibleBySkill(visible, true);
    }

    if (broadcast && visible) {
        bool deleted = false;
        {
            L2_GUARD(m_InfectedSkillsSect);
            deleted = _DeleteAbnormalStatus(ABNS_HIDE, this, nullptr, true, SPET_ETC, false);
        }
        if (deleted) {
            if (!IsPC()) {
                ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
            } else {
                CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
            }
        }
    }
    return SetVisible(visible);
}

bool CCreature::IsTargetable() { return m_data->m_StatusFlag[0]; }

bool CCreature::SetTargetable(bool targetable, bool flag) {
    L2_GUARD_FUNCTION();
    if (m_data->m_StatusFlag[0] == targetable) {
        return false;
    }

    m_data->m_StatusFlag[0] = targetable;
    if (!m_data->m_StatusFlag[0] && !targetable) {
        ResetAllTacticalSignToMeExcludeMyParty();
    }

    CWorldObject::SetTargetable(targetable, flag);
    if (IsNPC()) {
        CastNPC()->ParamChanged(NPCT_SETTARGETABLE);
    } else if (IsPC()) {
        ParamChanged(std::int64_t{PCB_CHAR_SETTARGETABLE});
    } else {
        ParamChanged();
    }
    return true;
}

bool CCreature::IsShowNametag() { return m_data->m_StatusFlag[1]; }

int CCreature::GetContainerIndex() { return m_data->container_index; }

double CCreature::GetCollisionRadius() const { return m_data->m_dCollisionRadius; }

FVector CCreature::GetMeleeAttackPosition(CCreature* attacker) {
    L2_GUARD_FUNCTION();

    const std::int32_t collisionDistance = runtime::AddInt32Wrapping(
        runtime::Win64CvttDoubleToInt32(m_data->m_dCollisionRadius),
        runtime::Win64CvttDoubleToInt32(attacker->m_data->m_dCollisionRadius)
    );
    const int attackDistance = runtime::Win64CvttDoubleToInt32(
        static_cast<double>(collisionDistance) + static_cast<double>(attacker->m_data->m_nAttackRange) * 0.5
    );

    const FVector attackerPosition = attacker->GetPos();
    const FVector targetPosition = GetDetailLocation();

    if (attackDistance / 100 < 5) {
        if (attackDistance / 100 < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] attack range invalid",
            //     kCreatureServerSourceFile,
            //     0x521a)
            // Log.Add(
            //     LOG_ERROR,
            //     "[movement] Invalid computed melee approach distance %d for attacker object %u and target object %u. Using
            //     the attacker position.", attackDistance, attacker->m_nId, m_nId)
            L2_LOG_ERROR(
                Log,
                "[movement] Invalid computed melee approach distance %d for attacker object %u and target object %u; using "
                "the attacker position.",
                attackDistance, attacker->m_nId, m_nId
            );
            return attackerPosition;
        }

        std::array<FVector, 12> candidates{};
        int candidateCount = 0;
        const double distance = static_cast<double>(attackDistance);
        for (std::size_t index = 0; index < 12; ++index) {
            FVector candidate{
                targetPosition.x + distance * g_cosSinTable.m_cosTable[index],
                targetPosition.y + distance * g_cosSinTable.m_sinTable[index],
                targetPosition.z + distance * 2.0,
            };

            CGeoData* const geo = CWorld::Inst().GetGeo();
            candidate.z = static_cast<double>(geo->FindGround(
                runtime::Win64CvttDoubleToInt32(candidate.x), runtime::Win64CvttDoubleToInt32(candidate.y),
                runtime::Win64CvttDoubleToInt32(candidate.z)
            ));
            if (CWorld::Inst().GetGeo()->MoveStraightTestBoth(
                    targetPosition, candidate, static_cast<double>(runtime::AddInt32Wrapping(attackDistance, 10)), true,
                    false, 0
                )) {
                candidates[static_cast<std::size_t>(candidateCount)] = candidate;
                ++candidateCount;
            }
        }

        if (candidateCount > 0 && !attacker->m_data->IsStraightAttacker()) {
            return candidates[static_cast<std::size_t>(std::rand() % candidateCount)];
        }

        const FVector direction{
            attackerPosition.x - targetPosition.x,
            attackerPosition.y - targetPosition.y,
            attackerPosition.z - targetPosition.z,
        };
        const FVector normalized = direction.SafeNormal();
        return FVector{
            targetPosition.x + distance * normalized.x,
            targetPosition.y + distance * normalized.y,
            targetPosition.z + distance * normalized.z,
        };
    }

    const FVector direction{
        attackerPosition.x - targetPosition.x,
        attackerPosition.y - targetPosition.y,
        attackerPosition.z - targetPosition.z,
    };
    const double scale = static_cast<double>(attackDistance) /
        std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    return FVector{
        targetPosition.x + direction.x * scale,
        targetPosition.y + direction.y * scale,
        targetPosition.z + direction.z * scale,
    };
}

void CCreature::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    CCreature* const creature = creatureId.GetCreature();
    const double ownCollisionRadius = GetCollisionRadius();
    const double creatureCollisionRadius = creature->GetCollisionRadius();
    const double collisionDistance = creatureCollisionRadius + ownCollisionRadius;

    if (static_cast<std::int8_t>(creature->m_FollowData.m_FollowType) > 0) {
        const int followDistance = creature->m_FollowData.m_FollowDistance;
        data->m_nAcceptableDistance = 0;
        data->m_nDistance =
            runtime::Win64CvttDoubleToInt32(static_cast<double>(followDistance) + collisionDistance + 10.0);
        return;
    }

    if (data->m_nSkillId < 1) {
        if (creature->IsPC() && creature->CastUser()->m_nCoupleID != 0) {
            const int coupleOption = creature->CastUser()->m_nCoupleOption;
            double distance = collisionDistance;
            if (coupleOption == 0x10 || coupleOption == 0x12) {
                distance += 10.0;
            } else if (coupleOption != 0x11) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid couple option %d",
                //     kCreatureServerSourceFile,
                //     0x56df,
                //     creature->CastUser()->m_nCoupleOption)
                // Log.Add(
                //     LOG_ERROR,
                //     "[movement] Cannot choose a couple-action distance to object %u: option %d is unsupported (later
                //     observation %d). Using distance 0.", creature->m_nId, coupleOption,
                //     creature->CastUser()->m_nCoupleOption)
                L2_LOG_ERROR(
                    Log,
                    "[movement] Cannot choose a couple-action distance to object %u: option %d is unsupported (later "
                    "observation %d); using distance 0.",
                    creature->m_nId, coupleOption, creature->CastUser()->m_nCoupleOption
                );
                data->m_nDistance = 0;
                return;
            }

            const int targetDistance = runtime::Win64CvttDoubleToInt32(distance);
            data->m_nDistance = targetDistance;
            data->m_nAcceptableDistance = targetDistance;
            return;
        }

        if (!data->m_bForceAttack && !IsEnemyTo(creature)) {
            if (creature->IsPC() && IsNPC()) {
                data->m_nDistance = 100;
                data->m_nAcceptableDistance = 110;
                return;
            }

            data->m_nDistance = runtime::Win64CvttDoubleToInt32(collisionDistance + 100.0);
            data->m_nAcceptableDistance = runtime::AddInt32Wrapping(data->m_nDistance, 10);
            return;
        }

        const int attackRange = creature->GetAttackRange();
        const double adjustedRange =
            attackRange < 0x12d ? static_cast<double>(attackRange) * 0.67 : static_cast<double>(attackRange - 100);
        data->m_nDistance = runtime::Win64CvttDoubleToInt32(adjustedRange + collisionDistance);
        data->m_nAcceptableDistance =
            runtime::Win64CvttDoubleToInt32(static_cast<double>(attackRange) + collisionDistance);
        return;
    }

    const int skillRange = data->m_nSkillRange;
    const double adjustedRange =
        skillRange < 0x12d ? static_cast<double>(skillRange) * 0.67 : static_cast<double>(skillRange - 100);
    data->m_nDistance = runtime::Win64CvttDoubleToInt32(adjustedRange + collisionDistance);
    data->m_nAcceptableDistance = runtime::Win64CvttDoubleToInt32(static_cast<double>(skillRange) + collisionDistance);
}

FAngle CCreature::GetDir() const { return m_data->GetDir(); }

int CCreature::GetIndex() const { return m_data->index; }

CObject* CCreature::Copy() { return nullptr; }

bool CCreature::IsCreature() { return true; }

CCreature* CCreature::CastCreature() { return this; }

bool CCreature::IsInVehicle() { return CContainerDB::IsVehicleContainerId(m_data->container_index); }

bool CCreature::IsInAirShip() { return m_data->m_AirShipId != 0; }

bool CCreature::IsInShuttle() { return CContainerDB::IsShuttleContainerId(m_data->container_index); }

bool CCreature::OnMagicSkillUsePacket(int skillId, bool first, bool second) {
    L2_GUARD_FUNCTION();
    if (IsInAirShip()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_ABOARD);
        return false;
    }

    if (IsPC()) {
        if (CastUser()->m_AreaMod.m_bIsInNoSkill) {
            SendSystemMessage(SystemMessageId::CANNOT_USE_SKILLS_IN_THIS_AREA);
            return false;
        }
    }

    SkillAction(skillId, first, second, nullptr, nullptr, 0, 0);
    return true;
}

bool CCreature::IsRealEnemyTo(CCreature* creature) {
    if (creature && creature->IsNPC()) {
        return m_data ? !m_data->m_bCanBeAttacked : false;
    }
    return m_data ? m_data->m_bCanBeAttacked : false;
}

bool CCreature::IsEnemyTo(CCreature* creature) { return IsRealEnemyTo(creature); }

bool CCreature::IsDuelAlly(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    if (IsSummonOrPet()) {
        CSummon* const summon = CastSummon();
        if (summon != nullptr) {
            User* const master = summon->GetMaster();
            if (master != nullptr && master->m_nId == creature->m_nId) {
                return true;
            }
        }
    }

    User* const user = GetUserOrMaster();
    return user != nullptr && user->IsDuelAlly(creature);
}

const char16_t* CCreature::GetName() const { return m_data->Name(); }

double CCreature::GetCollisionHeight() const { return m_data->m_dCollisionHeight; }

bool CCreature::IsActive() { return m_data->m_bIsActive; }

bool CCreature::IsAlive() { return m_data->m_bIsActive && m_data->m_bIsAlive != 0; }

char CCreature::GetGuilty() {
    if (User* const user = GetUserOrMaster()) {
        return user->GetGuilty();
    }
    if (IsSummonedNpc()) {
        if (User* const master = CastNPC()->GetMaster()) {
            return master->GetGuilty();
        }
    }
    return 0;
}

int CCreature::CalcCriminalRate() {
    if (User* const user = GetUserOrMaster()) {
        return user->CalcCriminalRate();
    }
    if (IsSummonedNpc()) {
        if (User* const master = CastNPC()->GetMaster()) {
            return master->CalcCriminalRate();
        }
    }
    return 0;
}

bool CCreature::IsRidingOnGround() { return m_data->m_rideType == ::RIDE_STRIDER || m_data->m_rideType == ::RIDE_WOLF; }

bool CCreature::IsRidingWolf() { return m_data->m_rideType == ::RIDE_WOLF; }

bool CCreature::IsRidingWyvern() const { return m_data->m_rideType == ::RIDE_WYVERN; }

bool CCreature::IsRidingStrider() { return m_data->m_rideType == ::RIDE_STRIDER; }

bool CCreature::IsRiding() { return m_data->m_rideType > ::RIDE_NONE; }

void CCreature::SetCollisionHeight(double height) { m_data->SetCollisionHeight(height); }

void CCreature::SetCollisionRadius(double radius) { m_data->SetCollisionRadius(radius); }

FVector CCreature::GetPos() const { return m_data->GetPos(); }

FVector CCreature::GetPosAsFollowTarget() const { return m_bMoving ? m_vMovingDest : m_data->GetPos(); }

CCreature::FollowData::FollowData() :
    m_FollowType(FOLLOW_NONE_TYPE),
    m_FollowDistance(-1),
    m_FollowAngle(0),
    m_FollowOffsetX(0),
    m_FollowOffsetY(0),
    m_FollowerSID(0) {}

void CCreature::FollowData::Init() {
    if (m_FollowType != FOLLOW_ABSOLUTE_DIST_TYPE) {
        m_FollowDistance = -1;
        m_FollowType = FOLLOW_NONE_TYPE;
        m_FollowAngle = 0;
        m_FollowOffsetX = 0;
        m_FollowOffsetY = 0;
    }
}

bool CCreature::FollowData::SetData(const char followType, const int first, const int second) {
    if (m_FollowType == followType) {
        if (followType == FOLLOW_DIST_TYPE && m_FollowDistance == first && m_FollowAngle == second) {
            return false;
        }
        if (followType == FOLLOW_OFFSET_TYPE && m_FollowOffsetX == first && m_FollowOffsetY == second) {
            return false;
        }
    }

    if (m_FollowType != FOLLOW_ABSOLUTE_DIST_TYPE) {
        Init();
    }

    if (followType == FOLLOW_DIST_TYPE || followType == FOLLOW_ABSOLUTE_DIST_TYPE) {
        m_FollowType = followType;
        m_FollowDistance = first;
        m_FollowAngle = second;
        return true;
    }

    if (followType != FOLLOW_OFFSET_TYPE) {
        return false;
    }

    m_FollowOffsetY = second;
    m_FollowType = FOLLOW_OFFSET_TYPE;
    m_FollowOffsetX = first;

    const std::uint32_t squaredDistance = static_cast<std::uint32_t>(first) * static_cast<std::uint32_t>(first) +
        static_cast<std::uint32_t>(second) * static_cast<std::uint32_t>(second);
    const std::int64_t signedSquaredDistance = squaredDistance <= 0x7fffffffU
        ? static_cast<std::int64_t>(squaredDistance)
        : static_cast<std::int64_t>(squaredDistance) - 0x100000000LL;
    m_FollowDistance = runtime::Win64CvttDoubleToInt32(std::sqrt(static_cast<double>(signedSquaredDistance)));
    return true;
}

void CCreature::InitializeBuilderCmdMod() {
    L2_GUARD_FUNCTION();
    m_BuilderCmdMod.m_nAbnormalChangeType = 3;
}

void CCreature::InitializeItemMod() {
    L2_GUARD_FUNCTION();
    m_ItemMod.m_bOnlyUseEquipedWeaponActiveSkill = false;
    m_ItemMod.m_bNoAttackWeapon = false;
    m_ItemMod.m_bHpLimitBreak = false;
}

void CCreature::InitializeAreaMod() {
    L2_GUARD_FUNCTION();

    m_AreaMod.m_bIsUnderMotherTree = false;
    m_AreaMod.m_bWasUnderMotherTree = false;
    m_AreaMod.m_bWasInDamage = false;
    m_AreaMod.m_bIsInDamage = false;
    m_AreaMod.m_bWasInPoison = false;
    m_AreaMod.m_bIsInPoison = false;
    m_AreaMod.m_bWasInSwamp = false;
    m_AreaMod.m_bIsInSwamp = false;
    m_AreaMod.m_bWasFrozen = false;
    m_AreaMod.m_bWasInInstantSkill = false;
    m_AreaMod.m_bIsInInstantSkill = false;
    m_AreaMod.m_bIsInBattleField = false;
    m_AreaMod.m_bWasInBattleField = false;
    m_AreaMod.m_bWasInPvPZone = false;
    m_AreaMod.m_bIsInPvPZone = false;
    m_AreaMod.m_bWasInSSQZone = false;
    m_AreaMod.m_bIsInSSQZone = false;
    m_AreaMod.m_bWasInPeaceField = false;
    m_AreaMod.m_bIsInPeaceField = false;
    m_AreaMod.m_bWasUnderWater = false;
    m_AreaMod.m_bIsUnderWater = false;
    m_AreaMod.m_bIsNearUnderWater = false;
    m_AreaMod.m_dSpeedModByArea = 1.0;
    m_AreaMod.m_dHpRegenAreaBonus = 0.0;
    m_AreaMod.m_dMpRegenAreaBonus = 0.0;
    m_AreaMod.m_nLeavingMessageNO = 0;
    m_AreaMod.m_dHeightDamage = 0.0;
}

void CCreature::InitializeSkillDurationMod() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillDurationMod.m_Lock);
    m_SkillDurationMod.Clear();
}

void CCreature::InitializeSkillMod() {
    L2_GUARD_FUNCTION();

    for (CCreatureSkillMod::Mod& mod : m_SkillMod.m_Mods) {
        mod.m_Per = 1.0;
        mod.m_Diff = 0.0;
    }
    for (CCreatureSkillMod::Mod& mod : m_SkillMod.m_MagicSpeedBonusByMagicType) {
        mod.m_Per = 1.0;
        mod.m_Diff = 0.0;
    }
    for (CCreatureSkillMod::SkillReact& react : m_SkillMod.m_SkillReacts) {
        for (int& probability : react.nProb) {
            probability = 0;
        }
    }
    for (double& probability : m_SkillMod.m_SkillRefectProb.m_Prob) {
        probability = 0.0;
    }

    m_SkillMod.m_bCanAct = true;
    m_SkillMod.m_bCanAttack = true;
    m_data->m_bCanAct = 1;
    m_SkillMod.m_bCanSpell = true;
    m_SkillMod.m_bCanPSkill = true;
    m_SkillMod.m_bCanSSkill = true;
    m_SkillMod.m_bCanTSkill = true;

    bool enableDamage = true;
    if (IsNPC()) {
        static_cast<void>(CastNPC());
        enableDamage = !m_data->m_bTeleporting;
    }
    if (enableDamage) {
        m_SkillMod.m_bCanGetDamageHP = true;
        m_SkillMod.m_bCanGetDamageMP = true;
    }

    m_SkillMod.m_dPKCountIncreaseProb = 0.0;
    for (std::size_t index = 0; index < 4; ++index) {
        m_SkillMod.m_dItemDropBonus[index] = 0.0;
        m_SkillMod.m_dExpDropBonus[index] = 0.0;
    }
    m_SkillMod.m_bIsReverseAttack = false;
    m_SkillMod.m_bIsAvoidAgro = false;
    m_SkillMod.m_bIsLowHPState = false;
    m_SkillMod.m_bIsHighHPState = false;
    m_SkillMod.m_dReverseDamagePer = 1.0;
    m_SkillMod.m_bIsVampiricState = false;
    m_SkillMod.m_dVampiricBonusPer = 1.0;
    m_SkillMod.m_dVampiricProb = 1.0;
    m_SkillMod.m_dVampiricProbNumerator = 1.0;
    m_SkillMod.m_dVampiricProbDenominator = 1.0;
    m_SkillMod.m_dVampiricDefencePer = 0.0;
    m_SkillMod.m_nMPVampiricIndex = 0;
    for (CCreatureSkillMod::MPVampiric& vampiric : m_SkillMod.m_MPVampiric) {
        vampiric.m_dProb = 0.0;
        vampiric.m_dBonus = 0.0;
    }

    m_SkillMod.m_nMaxEnchantIncreaseRate = 0;
    m_SkillMod.m_nWeaponEnchantRate = 0;
    m_SkillMod.m_nArmorEnchantRate = 0;
    m_SkillMod.m_nEnchantOption = NONE;
    m_SkillMod.m_dShieldDefenceModPer = 1.0;
    for (auto& moveModeMods : m_SkillMod.m_ModsMove) {
        for (CCreatureSkillMod::Mod& mod : moveModeMods) {
            mod.m_Per = 1.0;
            mod.m_Diff = 0.0;
        }
    }

    m_SkillMod.m_nManaChargeBonus = 0;
    m_SkillMod.m_nTransferDamageRatio = 0;
    m_SkillMod.m_TransferDamageTargetId = 0;
    m_SkillMod.m_nInfectedSkillDamageUPPer = 0;
    m_SkillMod.m_nInfectedSkillDamageUPDiff = 0;
    m_SkillMod.m_nAvoidAgroProb = 0;
    m_SkillMod.m_bSeeEvent = true;
    m_SkillMod.m_dPvpPhysicalAttackBonusPer = 1.0;
    m_SkillMod.m_dPvpPhysicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPvpMagicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPvePhysicalAttackBonusPer = 1.0;
    m_SkillMod.m_dPvePhysicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPveMagicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPvBossPhysicalAttackBonusPer = 1.0;
    m_SkillMod.m_dPvBossPhysicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPvBossMagicalSkillBonusPer = 1.0;
    m_SkillMod.m_dPvpPhysicalAttackDefenceBonusPer = 1.0;
    m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer = 1.0;
    m_SkillMod.m_dPvpMagicalSkillDefenceBonusPer = 1.0;
    m_SkillMod.m_bIsAllAngleShieldDefence = false;
    m_SkillMod.m_bIsSingleTarget = false;
    m_SkillMod.m_bAbnormalLimit = false;
    m_SkillMod.m_bBlockBuff = false;
    m_SkillMod.m_bBlockDeBuff = false;
    m_SkillMod.m_dBlockDeBuffDist = 0.0;
    m_SkillMod.m_eParamType = STE_MAX;
    m_SkillMod.m_dGoddBuffDispelBonusPer = 1.0;
    m_SkillMod.m_dBadBuffDispelBonusPer = 1.0;
    m_SkillMod.m_dGoddBuffAbnormalBonusPer = 1.0;
    m_SkillMod.m_dBadBuffAbnormalBonusPer = 1.0;
    for (double& bonus : m_SkillMod.m_dTradeBonusPer) {
        bonus = 1.0;
    }

    m_SkillMod.m_bCanCreateMPCC = false;
    m_SkillMod.m_nFishingChangeLevel = 0;
    m_SkillMod.m_dFishingChangeBonus = 0.0;
    m_SkillMod.m_bIsPKProtect = false;
    m_SkillMod.m_SoulMaxCount = 0;
    m_SkillMod.m_BarExpForSoulCollect = -1;
    m_SkillMod.m_cannotTransform = false;
    m_SkillMod.m_dMaxDecoSlotCount = 0;
    m_SkillMod.m_dMaxJewelSlotCount = 0;
    m_SkillMod.m_dMaxAgathionMainSlotCount = -1;
    m_SkillMod.m_dMaxAgathionSubSlotCount = -1;
    m_SkillMod.m_SubJobExpAdjustmentPer = 0;
    m_SkillMod.m_SubJobExpAdjustmentDiff = 0;
    m_SkillMod.m_SubJobVpAdjustmentPer = 0;
    m_SkillMod.m_SubJobVpAdjustmentDiff = 0;
    m_SkillMod.m_IsBerserkerMode = false;
    m_SkillMod.m_MPShieldPer = -1;
    m_SkillMod.m_MPShieldCostPer = 0;
    m_SkillMod.m_MaxEnergyCount = 0;
    m_SkillMod.m_nRechargeParam = 0;

    m_SkillMod.m_PVEDamageBonusInfo.m_dAmountDiff = 0.0;
    m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer = 0.0;
    m_SkillMod.m_PVPDamageBonusInfo.m_dAmountDiff = 0.0;
    m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer = 0.0;
    m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer = 0.0;
    m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountDiff = 0.0;
    m_SkillMod.m_dCreateItemCriticalProb = 0.0;
    m_SkillMod.m_dFishingMonsterProb = 0.0;
    m_SkillMod.m_dFishingStartTime = 0.0;
    m_SkillMod.m_nFishingInstantP = 0;

    {
        L2_GUARD(m_SkillMod.m_Lock);
        m_SkillMod.m_SkillTriggerMap.clear();
        m_SkillMod.m_SkillAttackTriggerMap.clear();
        m_SkillMod.m_SkillSkillTriggerMap.clear();
        m_SkillMod.m_SkillAvoidTriggerMap.clear();
        m_SkillMod.m_SkillDeathTriggerMap.clear();
        m_SkillMod.m_SkillAuraMap.clear();
        m_SkillMod.m_SkillSynergyAuraMap.clear();
        m_SkillMod.m_SkillMagicTypeTriggerMap.clear();
        m_SkillMod.m_SkillAbnormalSlotTriggerMap.clear();
        m_SkillMod.m_SkillAttackedSkillTriggerMap.clear();
        m_SkillMod.m_setBlockMagicType.clear();
        m_SkillMod.m_BlockedSlotType.clear();
        m_SkillMod.m_BlockedAddSlotType.clear();
        m_SkillMod.m_PVEPhysicalDefenceBonusMap.clear();
        m_SkillMod.m_PVEMagicDefenceBonusMap.clear();
        m_SkillMod.m_PVEAttrDefenceBonusMap.clear();
        m_SkillMod.m_SkillChangeTimeToAbnormalType.clear();
        m_SkillMod.m_SkillChangeTimeToMagicType.clear();
    }

    for (bool& value : m_bzAttackingTrait) {
        value = false;
    }
    for (double& value : m_dzAttackingTraitValue) {
        value = 1.0;
    }
    for (bool& value : m_bzDefendingTrait) {
        value = false;
    }
    for (bool& value : m_bzDefendingTraitPerfect) {
        value = false;
    }
    for (double& value : m_dzDefendingTraitValue) {
        value = 1.0;
    }
    for (double& bonus : m_SkillMod.m_ModSummonBonus) {
        bonus = 0.0;
    }

    m_maxCubic = 1;
    m_SkillMod.CriticalMaxUp.m_nPhysics = 0.0;
    m_SkillMod.CriticalMaxUp.m_nMagic = 0.0;
    if (!m_SkillMod.m_bIsDamageLimitedByStatus) {
        m_SkillMod.m_dDamageLimit = 0.0;
    }
    m_SkillMod.m_dRealRegist = 1.0;
    m_SkillMod.m_eChangeAttackPosition = SFPT_MAX;
    m_SkillMod.m_dRealTargetDispelProb = 3.0;
    for (CCreatureSkillMod::Mod& mod : m_SkillMod.m_ModMpCostAfterEffect) {
        mod.m_Per = 1.0;
        mod.m_Diff = 0.0;
    }
    m_SkillMod.m_dEnsoulAddProb = 0.0;
    m_SkillMod.m_dArmorPenetrateFactor = 1.0;
    m_SkillMod.m_dShieldPenetrateFactor = 1.0;
    m_SkillMod.m_dMagicalPenetrateFactor = 1.0;
    m_SkillMod.m_nActiveArtifactSlotGroup = 0;
}

CSkillInfo* CCreatureSkillMod::GetSynergySkillInfo(CCreature* creature, int synergyId) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return nullptr;
    }

    const auto foundSynergy = m_mapSynergySkill.find(synergyId);
    if (foundSynergy == m_mapSynergySkill.end() || foundSynergy->second == nullptr) {
        return nullptr;
    }
    const TriggerSynergySkillInfo& synergy = *foundSynergy->second;

    int requiredCount = 0;
    for (const CSharedCreatureData::AbnormalData& abnormal : creature->m_data->m_AbnormalStatus) {
        if (synergy.m_RequiredSlotType.find(abnormal.m_Type) != synergy.m_RequiredSlotType.end()) {
            ++requiredCount;
        }
    }
    if (static_cast<int>(synergy.m_RequiredSlotType.size()) != requiredCount) {
        return nullptr;
    }

    int optionCount = 0;
    for (const CSharedCreatureData::AbnormalData& abnormal : creature->m_data->m_AbnormalStatus) {
        if (synergy.m_SynergyOptionSlotType.find(abnormal.m_Type) != synergy.m_SynergyOptionSlotType.end()) {
            ++optionCount;
        }
    }
    if (optionCount <= 0) {
        return nullptr;
    }

    const auto foundSkill = synergy.m_mapSynergySkillByCount.find(optionCount);
    return foundSkill == synergy.m_mapSynergySkillByCount.end() ? nullptr : const_cast<CSkillInfo*>(foundSkill->second);
}

bool CCreatureSkillMod::CanGetDamageHP(CCreature* attacker, CCreature* target) {
    // Current L2Server 0x005e63a0.
    L2_GUARD_FUNCTION();
    if (!m_bCanGetRangeDamageHP) {
        // The current HP variant treats a missing endpoint as an allowed
        // non-ranged tick and bypasses the remaining gates.
        if (target == nullptr || attacker == nullptr) {
            return true;
        }
        const double distance = attacker->GetPos().DistCube(target->GetPos());
        if (distance >= m_dCanGetRangeDamageHPDistance && distance != m_dCanGetRangeDamageHPDistance) {
            return false;
        }
    }

    if (!m_bCanGetDamageHP) {
        CCreature::SendMagicAttckInfo(attacker, target, MagicAttackInfo::ImmuneForBadSkill);
        return false;
    }
    if (target == nullptr || attacker == nullptr) {
        return true;
    }

    if (attacker->IsPCOrPCRelatedNPC()) {
        attacker = attacker->GetUserOrMaster();
    }
    if (attacker == nullptr) {
        return false;
    }
    if (target->m_nDamageExceptAttackerId != 0 && target->m_nDamageExceptAttackerId != attacker->m_nId) {
        return false;
    }
    return target->m_nCanIgnoreDamageTargetId != attacker->m_nId;
}

bool CCreatureSkillMod::CanGetDamageMP(CCreature* attacker, CCreature* target) {
    // Current L2Server 0x005e6540.
    L2_GUARD_FUNCTION();
    if (!m_bCanGetRangeDamageMP) {
        if (target == nullptr || attacker == nullptr) {
            return false;
        }
        const double distance = attacker->GetPos().DistCube(target->GetPos());
        if (distance >= m_dCanGetRangeDamageMPDistance && distance != m_dCanGetRangeDamageMPDistance) {
            return false;
        }
    }

    if (!m_bCanGetDamageMP) {
        CCreature::SendMagicAttckInfo(attacker, target, MagicAttackInfo::ImmuneForBadSkill);
        return false;
    }
    if (target == nullptr || attacker == nullptr) {
        return true;
    }

    if (attacker->IsPCOrPCRelatedNPC()) {
        attacker = attacker->GetUserOrMaster();
    }
    if (attacker == nullptr) {
        return false;
    }
    if (target->m_nDamageExceptAttackerId != 0 && target->m_nDamageExceptAttackerId != attacker->m_nId) {
        return false;
    }
    return target->m_nCanIgnoreDamageTargetId != attacker->m_nId;
}

bool CCreatureSkillMod::NeedToAlterNormalDamage(CCreature& target) {
    // Current L2Server 0x005f17bc.
    if (m_stAlterNormalDamage.m_nAlterRatio == 0) {
        return false;
    }

    switch (m_stAlterNormalDamage.m_eType) {
        case ATT_ALL:
            return true;
        case ATT_PC:
            return target.IsPC();
        case ATT_NPC:
            return target.IsNPC();
        case ATT_SUMMON:
            return target.IsSummonOrPet();
        default:
            return false;
    }
}

CCreatureSkillMod::BonusInfoInCondition* CCreatureSkillMod::GetPVEPhysicalDefenceBonus(int category) {
    // Current L2Server 0x00604e84.
    L2_GUARD(m_Lock);
    const auto found = m_PVEPhysicalDefenceBonusMap.find(category);
    return found == m_PVEPhysicalDefenceBonusMap.end() ? nullptr : &found->second;
}

CCreatureSkillMod::BonusInfoInCondition* CCreatureSkillMod::GetPVEMagicDefenceBonus(int category) {
    // Current L2Server 0x00604f48.
    L2_GUARD(m_Lock);
    const auto found = m_PVEMagicDefenceBonusMap.find(category);
    return found == m_PVEMagicDefenceBonusMap.end() ? nullptr : &found->second;
}

CCreatureSkillMod::BonusInfoInCondition* CCreatureSkillMod::GetInfectedSkillChangeTime(
    DebuffType debuffType, int magicType
) {
    L2_GUARD(m_Lock);
    const auto found = m_SkillChangeTimeToMagicType.find(std::CDualKey<int, int>(static_cast<int>(debuffType), magicType));
    return found == m_SkillChangeTimeToMagicType.end() ? nullptr : &found->second;
}

CCreatureSkillMod::BonusInfoInCondition* CCreatureSkillMod::GetInfectedSkillChangeTime(int abnormalType, int magicType) {
    L2_GUARD(m_Lock);
    const auto found = m_SkillChangeTimeToAbnormalType.find(std::CDualKey<int, int>(abnormalType, magicType));
    return found == m_SkillChangeTimeToAbnormalType.end() ? nullptr : &found->second;
}

short* CCreatureSkillMod::GetPVEAttrDefenceBonus(int category) {
    L2_GUARD(m_Lock);
    const auto found = m_PVEAttrDefenceBonusMap.find(category);
    return found == m_PVEAttrDefenceBonusMap.end() ? nullptr : found->second;
}

void CCreature::InitAcquiredSkills() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AcquiredSkillsSect);
        m_AcquiredSkills.clear();
    }
    {
        L2_GUARD(m_SkillListSect);
        m_PassivePumpInfo.clear();
    }
}

void CCreature::EnterWorldImpl() {
    // Current L2Server 0x006282f4.
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::IncrementI32(&g_nHeartbeatingCreatureNum);
    runtime::raw_atomic::IncrementI32(&g_nExistingCreatureNum);

    m_bIsFreezing = false;
    m_data->m_nPKTimeStamp = 0;
    m_data->m_cIsGuilty = 0;
    m_bIsGMKill = false;
    m_nDead = 0;
    m_data->m_bGMInvisible = false;
    m_data->m_nSoulShot = 0;
    m_data->m_nFishingShot = 0;
    m_data->m_bSpiritShot = false;
    m_data->m_bCanBeResurrected = true;
    m_data->m_bCanResurrection = 0;
    m_data->m_nResurrectExp = 0;
    m_data->m_nResurrectRatio = 0;
    m_data->m_dwResurrectTime = runtime::GetTickCount();
    m_data->m_MovingStatus = MSTE_NOT_MOVING;
    m_data->m_StopMode = STP_STAND;
    m_data->m_MoveMode = MT_FAST;
    const std::int32_t environment = static_cast<std::int32_t>(m_data->m_moveType) == CMT_FLY ? ENV_AIR : ENV_GROUND;
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(m_data->m_Environment), environment, 0x53
    );
    m_data->m_FirstAttackerServerID = 0;
    m_data->m_LastAttackerServerID = 0;

    InitializeBuilderCmdMod();
    InitializeItemMod();
    InitializeSkillMod();
    InitializeAreaMod();
    InitializeSkillDurationMod();

    m_inv.m_nEXPRuneCount = 0;
    m_inv.m_nSPRuneCount = 0;
    m_inv.m_nRPRuneCount = 0;
    m_inv.m_RuneItemClassIds.clear();

    m_AreaMod.m_nWaterSurfaceHeight = runtime::Win64CvttDoubleToInt32(GetPos().z);
    // m_nCurrentWeight = 0.
    m_nCurrentWeight = WEIGHT_NORMAL;
    m_vSrcPos = FVector{};
    m_vMoveSkillDest = FVector{};
    m_bDecExp = true;
    m_nSetItemClassId = 0;
    {
        L2_GUARD(m_NewSetItemSkillLock);
        m_mapNewSetItem.clear();
    }
    {
        L2_GUARD(m_ShapeShiftedSetItemSkillLock);
        m_mapShapeShiftedSetItem.clear();
    }
    m_bIsInitiativeViolet = false;
    m_bIsSkillViolet = false;
    m_dHPRegenModByWeight = 1.0;
    m_dMPRegenModByWeight = 1.0;
    m_dCPRegenModByWeight = 1.0;
    m_dSpeedModByWeight = 1.0;
    m_nEnergyCount = 0;
    m_dwEnergyStartTime = 0;
    m_dwFakeDeathStartTime = 0;
    m_data->m_bIsSpoil = false;
    m_data->m_nCanSweeper = 0;
    m_data->m_nSpoilerSmartID = 0;
    m_data->m_bIsPlunded = false;
    m_data->m_bIsSow = true;
    m_data->m_bIsSeed2 = false;
    m_data->m_nCanHarvest = 0;
    m_data->m_nSowerSmartID = 0;
    m_data->m_dOverHitBonus = 1.0;
    m_maxCubic = 1;
    m_InstantDurationSkill = InfectedSkillType{};
    m_InstantDurationSkill.pSkillInfo = nullptr;
    m_dSpiritShotHealBonus = 0.0;
    m_sNowSoulShot = 0;
    m_bIsSysMsg = true;
    m_bIsHP1SysMsg = false;
    m_bIsHP2SysMsg = false;
    m_bIsNight = g_L2Time.m_bIsNight;

    const double lowHpLevel1 = m_data->m_dBaseHP * 0.3;
    if (lowHpLevel1 < m_data->m_dHP) {
        m_bIsLowHPLevel1 = false;
    } else {
        m_bIsHP1SysMsg = true;
        m_bIsLowHPLevel1 = true;
    }
    const double lowHpLevel2 = m_data->m_dBaseHP * 0.6;
    if (lowHpLevel2 < m_data->m_dHP) {
        m_bIsLowHPLevel2 = false;
    } else {
        m_bIsHP2SysMsg = true;
        m_bIsLowHPLevel2 = true;
    }

    m_dwMPowerStorage = 0;
    m_sPowerCount = -1;
    m_bIsRestarting = false;
    m_data->m_bIsFakeDeath = false;
    m_data->m_bFakeDeathFail = false;
    m_data->m_nMaxMagicLevel = 0;

    if (IsPC()) {
        m_data->SetAlive(m_nDownExp == 0);
    } else if (IsPet()) {
        m_data->SetAlive(m_bIsPetAlive == 1);
        m_bIsPetAlive = 1;
    } else {
        m_nDownExp = 0;
        m_data->SetAlive(true);
    }
    if (IsPet() && !IsAlive() && m_nDeathCount.load(std::memory_order_seq_cst) > 0) {
        m_data->m_bDecayComplete = true;
    }

    m_bLeft = false;
    m_nAuraTick = 0;
    m_nTickCount = 0;
    m_nCommitTick = 0;
    m_nCommitTick2 = 0;
    m_bParamChanged.exchange(0, std::memory_order_seq_cst);
    m_nValidateChanged = VB_MAX;
    m_bAbnormalStatusChanged = false;
    m_bValidateChanged = false;
    m_bValidateExChanged = false;
    m_nValidateExChanged = 4;
    SetInCombatMode(false);
    m_nCombatModeTimeStamp = 0;
    m_nSummonLifeLastTick = runtime::GetTickCount();
    m_nBowDelay = 0;
    ::CIOObject::SetShortTimeStamp(&m_nBowDelay, 0);
    m_data->m_nLastAttackTick = 0;
    m_eDeadDamage = DAMAGE_NORMAL;

    AcquireSkills(g_SkillAcquireDB.m_vectorClsSkill[static_cast<std::size_t>(m_data->m_nClass)], true, false);
    AcquireSkills(g_SkillAcquireDB.m_vectorRaceSkill[static_cast<std::size_t>(m_data->m_Race)], true, false);
    ValidateChanged(VB_MAX);

    {
        L2_GUARD(m_PledgeContributeSect);
        m_PledgeContributes.clear();
        m_nPledgeContributeSaveTick = 0;
    }
    m_nCurrentPassingPos = 0;
    m_nCurrentPassingPosSize = 0;
    for (FVector& passingPosition : m_vPassingPos) {
        passingPosition = GetPos();
    }
    InitMes();

    if (nHeartBeat != 666) {
        // Log.Add(LOG_ERROR, L"nHeartBeat!=666!")
        // Log.Add(
        //     LOG_WARN,
        //     "[creature] Unexpected heartbeat interval %d ms while entering object %u into the world. Restoring 666 ms.",
        //     nHeartBeat,
        //     m_nId)
        L2_LOG_WARN(
            Log, "[creature] Unexpected heartbeat interval %d ms while entering object %u into the world; restoring 666 ms.",
            nHeartBeat, m_nId
        );
        // Noise: Duplicate of the preceding heartbeat-correction warning. The original interval reset remains.
        // Log.Add(LOG_ERROR, L"nHeartBeat!=666!")
        // Noise: Duplicate of the preceding heartbeat-correction warning. The original interval reset remains.
        // Log.Add(LOG_ERROR, L"nHeartBeat!=666!")
        // Noise: Duplicate of the preceding heartbeat-correction warning. The original interval reset remains.
        // Log.Add(LOG_ERROR, L"nHeartBeat!=666!")
        // Noise: Duplicate of the preceding heartbeat-correction warning. The original interval reset remains.
        // Log.Add(LOG_ERROR, L"nHeartBeat!=666!")
        nHeartBeat = 666;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    const FVector fieldPosition = m_data->m_Pos;
    m_data->m_bIsFreezed = geo->IsField(
        runtime::Win64CvttDoubleToInt32(fieldPosition.x), runtime::Win64CvttDoubleToInt32(fieldPosition.y),
        runtime::Win64CvttDoubleToInt32(fieldPosition.z), GEO_FIELD_FREEZE
    );

    const FVector areaPosition = GetPos();
    const int areaX = runtime::Win64CvttDoubleToInt32(areaPosition.x);
    const int areaY = runtime::Win64CvttDoubleToInt32(areaPosition.y);
    CGeoZone* const zone = geo->GetZone(
        (runtime::AddInt32Wrapping(areaX, 0x50000) >> 15) + 10, (runtime::AddInt32Wrapping(areaY, 0x40000) >> 15) + 10
    );
    if (zone != nullptr) {
        CGeoSector* const sector = zone->GetSector((areaX >> 7) & 0xff, (areaY >> 7) & 0xff);
        if (sector != nullptr) {
            AreaCheckForEveryTick(zone, sector, -1);
        }
    }

    int randomDelay = 1;
    if (nHeartBeat < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nHigh [%d] in Random",
        //     nHeartBeat)
        // Log.Add(
        //     LOG_ERROR,
        //     "[creature] Invalid heartbeat interval %d ms while scheduling object %u. Keeping the initial random offset of
        //     1 ms.", nHeartBeat, m_nId)
        L2_LOG_ERROR(
            Log,
            "[creature] Invalid heartbeat interval %d ms while scheduling object %u; keeping the initial random offset of 1 "
            "ms.",
            nHeartBeat, m_nId
        );
    } else {
        randomDelay = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * static_cast<double>(nHeartBeat) + 1.0);
    }
    AddTimer(static_cast<std::int64_t>(runtime::AddInt32Wrapping(nHeartBeat, randomDelay)), 0);
}

void CCreature::SetMaxMagicLevel() {
    L2_GUARD_FUNCTION();
    m_data->m_nMaxMagicLevel = 1;
    L2_GUARD(m_AcquiredSkillsSect);
    for (const auto& acquiredSkill : m_AcquiredSkills) {
        const AcquiredSkillInfo& acquired = acquiredSkill.second;
        const CSkillKey skillKey(acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel);
        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(skillKey, this);
        if (acquired.m_nId > 0 && acquired.m_nId < 2000 && skillInfo != nullptr &&
            static_cast<std::uint32_t>(skillInfo->m_nIsMagic) + 2U > 1U &&
            m_data->m_nMaxMagicLevel < skillInfo->m_nMagicLevel) {
            m_data->m_nMaxMagicLevel = skillInfo->m_nMagicLevel;
        }
    }
}

bool CCreature::IsIgnoreSkill(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillDurationMod.m_Lock);

    int count = 0;
    for (const auto& ignoredSkill : m_SkillDurationMod.m_mapIgnoreSkillId) {
        if (ignoredSkill.first == skillId) {
            return true;
        }

        ++count;
        if (count % 1000 == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"IsIgnoreSkill (too many ignore skills) name [%s] "
            //     L"skillID [%d] count [%d]",
            //     GetName(),
            //     skillId,
            //     count)
            // Log.Add(
            //     LOG_WARN,
            //     L"[skills] Ignore-skill lookup for %s has not found skill %d after scanning %d entries (map size %zu)
            //     continuing.", GetName(), skillId, count, m_SkillDurationMod.m_mapIgnoreSkillId.size())
            // L2_LOG_WARN(Log,
            //     L"[skills] Ignore-skill lookup for %s has not found skill %d after scanning %d entries (map size %zu)
            //     continuing.", GetName(), skillId, count, m_SkillDurationMod.m_mapIgnoreSkillId.size())
            L2_LOG_WARN(
                Log,
                u"[skills] Ignore-skill lookup for %s has not found skill %d after scanning %d entries (map size %zu); "
                u"continuing.",
                GetName(), skillId, count, m_SkillDurationMod.m_mapIgnoreSkillId.size()
            );
            // Noise: The preceding scan warning already includes the map size. Repeating it with raw addresses adds no
            // lookup outcome. Log.Add(
            //     LOG_ERROR,
            //     L"IsIgnoreSkill (too many ignore skills) map ignore "
            //     L"skill ID address [%llX] (*it) address [%llX] "
            //     L"size [%d]",
            //     static_cast<unsigned long long>(
            //         reinterpret_cast<std::uintptr_t>(
            //             &m_SkillDurationMod.m_mapIgnoreSkillId)),
            //     static_cast<unsigned long long>(
            //         reinterpret_cast<std::uintptr_t>(
            //             &ignoredSkill.first)),
            //     static_cast<int>(
            //         m_SkillDurationMod.m_mapIgnoreSkillId.size()))
        }
    }
    return false;
}

bool CCreature::IsLockedBuff(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_BuffLockSect);
    return m_BuffLockMap.find(skillId) != m_BuffLockMap.end();
}

int CCreature::PrepareSpelledInfoChangedBuffer(char* const buffer, int& capacity) {
    // Current L2Server 0x00601484. The wire representation deliberately
    // uses the low 16 bits of level/sub-level and switches to the extended
    // duration tail only when the remaining time no longer fits in int16.
    L2_GUARD_FUNCTION();
    int written = 0;
    int count = 0;

    L2_GUARD(m_InfectedSkillsSect);
    std::size_t skillCount = m_InfectedSkills.size();
    if (skillCount > 108U) {
        // Log.Add(
        //     LOG_ERROR,
        //     "abnormal status exceeds MAX_ABNORMAL_STATUS at file[%s], "
        //     "line[%d]",
        //     "Creature_server.cpp",
        //     0x5e9d)
        // Log.Add(
        //     LOG_ERROR,
        //     "[skills] Object %u has %zu abnormal effects. Limiting this status update to the first 108 entries.",
        //     m_nId, skillCount)
        L2_LOG_ERROR(
            Log, "[skills] Object %u has %zu abnormal effects; limiting this status update to the first 108 entries.", m_nId,
            skillCount
        );
        skillCount = 108U;
    }

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    for (std::size_t index = 0; index < skillCount; ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
        CSkillInfo* const skill = infected.pSkillInfo;
        if (IsLockedBuff(skill->m_nSkillId)) {
            continue;
        }

        int remaining = -1;
        if (skill->m_nAbnormalTime != -1 && skill->m_OperateType != SKILL_AURA_ACTIVITY &&
            skill->m_OperateType != SKILL_AURA_ACTIVITY2) {
            const int elapsed = now - infected.nStartTime;
            const int duration = skill->m_nAbnormalTime == -2 ? infected.nDuration : GetAbnormalTime(skill, false);
            remaining = std::max(0, duration - elapsed);
        }

        const std::uint32_t sourceCreatureId =
            static_cast<int>(skill->m_nSubAbnormal) >= 0 && static_cast<int>(skill->m_nSubAbnormal) < ABNS_MAX
            ? infected.nSourceCreatureId
            : 0U;
        const std::uint32_t levelAndSubLevel = static_cast<std::uint32_t>(static_cast<std::uint16_t>(skill->m_nLevel)) |
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(skill->m_nSubLevel)) << 16U);

        const int assembled = remaining < 0x8000 ? ::Assemble(
                                                       buffer + written, capacity - written, "dddh", skill->m_nSkillId,
                                                       levelAndSubLevel, sourceCreatureId, remaining
                                                   )
                                                 : ::Assemble(
                                                       buffer + written, capacity - written, "dddhd", skill->m_nSkillId,
                                                       levelAndSubLevel, sourceCreatureId, 0x7fff, remaining
                                                   );
        if (assembled < 1) {
            break;
        }
        written += assembled;
        ++count;
    }

    capacity = written;
    return count;
}

void CCreature::_CheckBuffLock() {
    L2_GUARD_FUNCTION();

    std::map<AbnormalStatusSlotType, int> abnormalStatusMap;
    L2_GUARD(m_BuffLockSect);
    ::std::map<int, const CSkillInfo*> previousBuffLocks(std::move(m_BuffLockMap));

    if (m_InfectedSkills.size() > 200) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] [%d] Too many infected skills. size [%d]",
        //     kCreatureServerSourceFile,
        //     0x5907,
        //     static_cast<int>(m_InfectedSkills.size()))
        // Log.Add(
        //     LOG_WARN,
        //     "[skills] Rebuilding buff locks for object %u with %zu abnormal effects (more than 200). Continuing through
        //     the full list.", m_nId, m_InfectedSkills.size())
        L2_LOG_WARN(
            Log,
            "[skills] Rebuilding buff locks for object %u with %zu abnormal effects (more than 200); continuing through the "
            "full list.",
            m_nId, m_InfectedSkills.size()
        );
    }

    const int threadId = CThreadManager::GetThreadID();
    l2server46_20160218_CheckBuffInfo& checker = g_CheckBuffInfo[threadId];
    {
        GuardHelper infectedSkillsLoop(L"m_InfectedSkills loop start", CThreadManager::GetThreadID());
        for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
            const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
            const CSkillInfo* const skill = infected.pSkillInfo;
            const AbnormalStatusSlotType abnormalType = skill->m_nAbnormalType;
            const auto found = abnormalStatusMap.find(abnormalType);

            if (found == abnormalStatusMap.end()) {
                GuardHelper insertion(L"abnormalStatusMap.insert 2", CThreadManager::GetThreadID());
                checker.WriteCheckBuffInfo(
                    static_cast<int>(GetDBID()), static_cast<int>(abnormalType), skill->m_nSkillId, static_cast<int>(index)
                );
                abnormalStatusMap.emplace(abnormalType, static_cast<int>(index));
                checker.Clear();
                continue;
            }

            const InfectedSkillType& previous =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(found->second));
            const CSkillInfo* const previousSkill = previous.pSkillInfo;
            if (previous.nSourceCreatureId == infected.nSourceCreatureId ||
                (previousSkill->m_eDebuff != DB_BAD && previousSkill->m_eDebuff != DB_PHYSICAL_MES)) {
                if (skill->m_nAbnormalLv < previousSkill->m_nAbnormalLv ||
                    (!skill->m_bInstantItem && previousSkill->m_bInstantItem)) {
                    GuardHelper insertion(L"m_BuffLockMap.insert 1", CThreadManager::GetThreadID());
                    checker.WriteCheckBuffInfo(
                        static_cast<int>(GetDBID()), static_cast<int>(abnormalType), skill->m_nSkillId,
                        static_cast<int>(index)
                    );
                    m_BuffLockMap.emplace(skill->m_nSkillId, skill);
                    checker.Clear();
                } else {
                    GuardHelper insertion(L"m_BuffLockMap.insert 2", CThreadManager::GetThreadID());
                    checker.WriteCheckBuffInfo(
                        static_cast<int>(GetDBID()), static_cast<int>(abnormalType), previousSkill->m_nSkillId,
                        static_cast<int>(index)
                    );
                    m_BuffLockMap.emplace(previousSkill->m_nSkillId, previousSkill);
                    checker.Clear();
                }
            } else {
                GuardHelper insertion(L"abnormalStatusMap.insert 1", CThreadManager::GetThreadID());
                checker.WriteCheckBuffInfo(
                    static_cast<int>(GetDBID()), static_cast<int>(abnormalType), skill->m_nSkillId, static_cast<int>(index)
                );
                abnormalStatusMap.emplace(abnormalType, static_cast<int>(index));
                checker.Clear();
            }
        }
    }

    if (previousBuffLocks.empty() && m_BuffLockMap.empty()) {
        return;
    }

    std::set<int> previousSkillIds;
    for (const auto& entry : previousBuffLocks) {
        previousSkillIds.insert(entry.first);
    }
    std::set<int> currentSkillIds;
    for (const auto& entry : m_BuffLockMap) {
        currentSkillIds.insert(entry.first);
    }

    std::vector<int> unlockedSkillIds(previousSkillIds.size());
    const auto unlockedEnd = std::set_difference(
        previousSkillIds.begin(), previousSkillIds.end(), currentSkillIds.begin(), currentSkillIds.end(),
        unlockedSkillIds.begin()
    );
    std::vector<int> lockedSkillIds(currentSkillIds.size());
    const auto lockedEnd = std::set_difference(
        currentSkillIds.begin(), currentSkillIds.end(), previousSkillIds.begin(), previousSkillIds.end(),
        lockedSkillIds.begin()
    );

    for (auto skillId = unlockedSkillIds.begin(); skillId != unlockedEnd; ++skillId) {
        const CSkillInfo* const skill = previousBuffLocks[*skillId];
        if (skill != nullptr) {
            PushSerializedMessage(new CSerializedOnInfectedSkillByMessage(this, this, *skill, SPST_SKILL_UNLOCK, false));
        }
    }
    for (auto skillId = lockedSkillIds.begin(); skillId != lockedEnd; ++skillId) {
        const CSkillInfo* const skill = m_BuffLockMap[*skillId];
        if (skill != nullptr) {
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skill, SPET_SKILL_LOCK));
        }
    }
}

int CCreature::GetSkillReuseDelayGroup(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    const auto found = m_AcquiredSkills.find(skillId);
    return found == m_AcquiredSkills.end() ? -1 : found->second.m_ReuseDelayGroup;
}

CSkillInfo* CCreature::GetActiveSkill(int skillId, int& level, int& subLevel) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_ActiveSkills.find(skillId);
    if (found == m_ActiveSkills.end()) {
        return nullptr;
    }

    CSkillInfo* const skillInfo = const_cast<CSkillInfo*>(found->second);
    level = skillInfo->m_nLevel;
    subLevel = skillInfo->m_nSubLevel;
    return skillInfo;
}

const CSkillInfo* CCreature::GetUsableSkill(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);

    const auto active = m_ActiveSkills.find(skillId);
    if (active != m_ActiveSkills.end()) {
        return active->second;
    }
    const auto aura = m_AuraSkills.find(skillId);
    if (aura != m_AuraSkills.end()) {
        return aura->second.first;
    }
    const auto toggle = m_ToggleSkills.find(skillId);
    return toggle == m_ToggleSkills.end() ? nullptr : toggle->second.first;
}

int CCreature::GetAlterSkill(const CSkillInfo** skillInfo) {
    // Current L2Server 0x00628c0c. The target copies the
    // complete map before walking it, then tests the current target's
    // abnormal slots in key order.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr || *skillInfo == nullptr) {
        return 0;
    }

    CSkillInfo::AbnormalAlterSkillMap alterSkills = (*skillInfo)->m_AlterSkillList;
    CCreature* const target = m_spTarget.GetCreature();
    if (target != nullptr) {
        for (auto& entry : alterSkills) {
            if (!target->m_data->IsAbnormalType(entry.first)) {
                continue;
            }
            AlterSkillInfo* const replacement = CSkillInfo::GetAlterSkillByProb(entry.second);
            if (replacement != nullptr && replacement->pSkillInfo != nullptr) {
                *skillInfo = replacement->pSkillInfo;
            }
            break;
        }
    }
    return (*skillInfo)->m_nSkillId;
}

CSkillInfo* CCreature::GetAuraSkill(int skillId, int& level, int& subLevel, bool& enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_AuraSkills.find(skillId);
    if (found == m_AuraSkills.end()) {
        return nullptr;
    }

    CSkillInfo* const skillInfo = const_cast<CSkillInfo*>(found->second.first);
    level = skillInfo->m_nLevel;
    subLevel = skillInfo->m_nSubLevel;
    enabled = found->second.second;
    return skillInfo;
}

bool CCreature::AuraSkillOnOff(int skillId, bool enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_AuraSkills.find(skillId);
    if (found == m_AuraSkills.end()) {
        return false;
    }
    found->second.second = enabled;
    return true;
}

CSkillInfo* CCreature::GetToggleSkill(int skillId, int& level, int& subLevel, bool& enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_ToggleSkills.find(skillId);
    if (found == m_ToggleSkills.end()) {
        return nullptr;
    }

    CSkillInfo* const skillInfo = const_cast<CSkillInfo*>(found->second.first);
    level = skillInfo->m_nLevel;
    subLevel = skillInfo->m_nSubLevel;
    enabled = found->second.second;
    return skillInfo;
}

void CCreature::ReportInfectedSkills(CUserSocket* socket) {
    // Current L2Server 0x00620864.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        if (!IsPC()) {
            return;
        }
        socket = CastUser()->GetSocket();
    }

    std::optional<std::vector<InfectedSkillType>> infectedSkillsStorage;
    {
        L2_GUARD(m_InfectedSkillsSect);
        infectedSkillsStorage.emplace(m_InfectedSkills);
    }

    auto& infectedSkills = *infectedSkillsStorage;
    std::array<char16_t, 256> message{};
    const std::int32_t infectedSkillCount = runtime::Int32FromBits(static_cast<std::uint32_t>(infectedSkills.size()));
    FormatUtf16To(message.data(), message.size(), u"infected skills : total %d", infectedSkillCount);
    socket->SendSystemMessage(u"Skill", message.data());

    std::int32_t current = 0;
    CIOObject::SetTimeStamp(&current);
    for (const InfectedSkillType& infected : infectedSkills) {
        CSkillInfo* const skill = infected.pSkillInfo;
        const int duration = skill->m_nAbnormalTime == -2 ? infected.nDuration : GetAbnormalTime(skill, false);
        const int remaining = skill->m_nAbnormalTime == -1
            ? -1
            : runtime::SubtractInt32Wrapping(runtime::AddInt32Wrapping(infected.nStartTime, duration), current);
        FormatUtf16To(
            message.data(), message.size(),
            u"name:[%s] slot:[%d] level:[%d] start:[%d] "
            u"current:[%d] remain:[%d]",
            skill->m_pszName, static_cast<int>(skill->m_nAbnormalType), skill->m_nLevel, infected.nStartTime, current,
            remaining
        );
        socket->SendSystemMessage(u"Skill", message.data());
    }
}

int CCreature::GetAbnormalTime(const CSkillInfo* skillInfo, bool ignoreSkillMod) {
    L2_GUARD_FUNCTION();

    int abnormalTime = 0;
    if (skillInfo != nullptr) {
        abnormalTime = skillInfo->m_nAbnormalTime;
        if (abnormalTime == 0) {
            abnormalTime = IsPC() ? skillInfo->m_nPVPAbnormalTime : skillInfo->m_nPVEAbnormalTime;
        }
    }

    if (IsPC() && !ignoreSkillMod) {
        CCreatureSkillMod::BonusInfoInCondition* bonus =
            m_SkillMod.GetInfectedSkillChangeTime(static_cast<int>(skillInfo->m_nAbnormalType), skillInfo->m_nIsMagic);
        if (bonus == nullptr) {
            DebuffType debuffType = skillInfo->m_eDebuff;
            if (debuffType == DB_PHYSICAL_MES || debuffType == DB_MENTAL_MES) {
                debuffType = DB_BAD;
            }
            bonus = m_SkillMod.GetInfectedSkillChangeTime(debuffType, skillInfo->m_nIsMagic);
        }
        if (bonus != nullptr) {
            abnormalTime = runtime::Win64CvttDoubleToInt32(
                static_cast<double>(abnormalTime) * bonus->m_dAmountPer + bonus->m_dAmountDiff
            );
        }
    }

    return abnormalTime < 0 ? 0 : abnormalTime;
}

void CCreature::GetInfectedSkillsToDBSave(std::vector<InfectedSkillType>& infectedSkills) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_InfectedSkillsSect);
    for (const InfectedSkillType& infected : m_InfectedSkills) {
        CSkillInfo* const skillInfo = infected.pSkillInfo;
        if (skillInfo == nullptr || skillInfo->m_bExceptRestore || infected.IsChannellingSkill() ||
            skillInfo->m_OperateType == SKILL_SELFDURATION || skillInfo->m_bDeleteBuff ||
            skillInfo->m_nSkillId == g_nClanAdventSkill || CMentorSetting::inst().IsMentorBuff(skillInfo->m_nSkillId) ||
            CMentorSetting::inst().IsMenteeBuff(skillInfo->m_nSkillId) ||
            skillInfo->m_nAbnormalType == static_cast<AbnormalStatusSlotType>(0x1d0) ||
            skillInfo->m_nAbnormalType == static_cast<AbnormalStatusSlotType>(0x284) || skillInfo->m_nAbnormalTime == -2) {
            continue;
        }
        infectedSkills.push_back(infected);
    }
}

void CCreature::PackingInfectedSkillsToDBSave(const std::vector<InfectedSkillType>& infectedSkills, IPacketHelper& packet) {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    for (const InfectedSkillType& infected : infectedSkills) {
        const CSkillInfo* const skillInfo = infected.pSkillInfo;
        if (skillInfo == nullptr) {
            continue;
        }

        const std::int32_t endTime = runtime::AddInt32Wrapping(
            GetAbnormalTime(skillInfo, true), runtime::SubtractInt32Wrapping(infected.nStartTime, now)
        );
        packet.Assemble(
            "ddddd", skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, endTime, infected.bSelfCast
        );
    }
}

bool CCreature::IsCriticalInfected(const CSkillInfo* const skillInfo) const {
    if (skillInfo->m_nSpecialLevel < 0 && skillInfo->m_nSpecialLevel > -5) {
        return false;
    }

    double property = 0.0;
    switch (m_SkillMod.m_eParamType) {
        case STE_STR:
            property = StrBonusF();
            break;
        case STE_DEX:
            property = DexBonusF();
            break;
        case STE_CON:
            property = ConBonusF();
            break;
        case STE_INT:
            property = IntBonusF();
            break;
        case STE_MEN:
            property = MenBonusF();
            break;
        case STE_WIT:
            property = WitBonusF();
            break;
        default:
            break;
    }

    double probability = property * m_SkillMod.m_Mods[0x2b].m_Per + m_SkillMod.m_Mods[0x2b].m_Diff;
    if (skillInfo->m_nIsMagic == SIMT_MAGIC_SKILL || skillInfo->m_nIsMagic == SIMT_MAGIC_SKILL_NOCANCEL) {
        probability *= 0.5;
    }
    return genrand64_real1() * 100.0 < probability;
}

InfectedSkillType CCreature::CreateInfectedSkillType(
    CCreature* const attacker,
    const CSkillInfo* const skillInfo,
    const CSkillInfo* const parentSkillInfo,
    const int infectedTime
) {
    L2_GUARD_FUNCTION();

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);

    InfectedSkillType infected;
    infected.pSkillInfo = const_cast<CSkillInfo*>(skillInfo);
    infected.bSelfCast = attacker != nullptr && attacker->GetIndex() == GetIndex();

    if (parentSkillInfo != nullptr) {
        const int abnormalTime = GetAbnormalTime(skillInfo, false);
        infected.nStartTime = runtime::Win64CvttDoubleToInt32(
            static_cast<double>(runtime::SubtractInt32Wrapping(now, abnormalTime)) + parentSkillInfo->m_dSkillHitTime
        );
    } else if (infectedTime != 0) {
        infected.nStartTime = infectedTime;
    } else if (skillInfo->m_nAbnormalTime == -1) {
        infected.nDuration = -1;
        infected.nStartTime = -1;
    } else {
        const int abnormalTime = GetAbnormalTime(skillInfo, false);
        infected.nDuration = abnormalTime;
        infected.nStartTime = now;
        if (attacker != nullptr && attacker->IsCriticalInfected(skillInfo) && skillInfo->m_bAbnormalTimeCritical) {
            infected.nStartTime = runtime::AddInt32Wrapping(infected.nStartTime, abnormalTime);
            if (attacker->IsPC()) {
                User* const user = attacker->CastUser();
                if (user != nullptr) {
                    user->SendSystemMessage(SystemMessageId::SKILL_MASTERY_EXTENDED_EFFECT_DURATION);
                }
            }
        }
        infected.nReduceCount = skillInfo->m_nReduceCount;
        infected.dReduceDamage = skillInfo->m_dReduceDamage;
    }

    infected.m_pChannellingSkillInfo = const_cast<CSkillInfo*>(parentSkillInfo);
    if (attacker == nullptr) {
        infected.nSourceCreatureId = 0;
    } else {
        infected.nSourceCreatureId = attacker->GetSmartID().Sid();
        if (parentSkillInfo != nullptr) {
            infected.m_SourceCreatureIds.push_back(static_cast<int>(attacker->m_nId));
        }
    }
    return infected;
}

int CCreature::_FindSameBuff(
    const CSkillInfo* const skillInfo,
    bool& differentInstantItem,
    int& buffCount,
    int& debuffCount,
    int& songDanceCount,
    int& triggerCount,
    CCreature* const attacker
) const {
    L2_GUARD_FUNCTION();

    int sameIndex = -1;
    differentInstantItem = false;
    for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
        const CSkillInfo* const current = infected.pSkillInfo;
        const bool sourceSpecific = attacker != nullptr && skillInfo->m_eDebuff >= DB_BAD &&
            skillInfo->m_eDebuff <= DB_MENTAL_MES && static_cast<int>(skillInfo->m_nSubAbnormal) >= 0 &&
            static_cast<int>(skillInfo->m_nSubAbnormal) <= 0x286;

        if (!sourceSpecific) {
            if (current->m_nAbnormalType != skillInfo->m_nAbnormalType) {
                continue;
            }
            if (current->m_bInstantItem == skillInfo->m_bInstantItem) {
                sameIndex = static_cast<int>(index);
            } else {
                differentInstantItem = true;
            }
            continue;
        }

        if (current->m_nSubAbnormal == skillInfo->m_nSubAbnormal && current->m_nAbnormalType == skillInfo->m_nAbnormalType &&
            infected.nSourceCreatureId == static_cast<unsigned int>(attacker->m_nId)) {
            sameIndex = static_cast<int>(index);
        }
    }

    std::set<AbnormalStatusSlotType> buffs;
    std::set<AbnormalStatusSlotType> debuffs;
    std::set<AbnormalStatusSlotType> songs;
    std::set<AbnormalStatusSlotType> triggers;
    for (const InfectedSkillType& infected : m_InfectedSkills) {
        const CSkillInfo* const current = infected.pSkillInfo;
        const int magicType = NormalizeSkillMagicType(current->m_nIsMagic);
        if (magicType == SIMT_SONGDANCE_SKILL) {
            songs.insert(current->m_nAbnormalType);
        } else if (magicType == SIMT_TRIGGER_SKILL) {
            triggers.insert(current->m_nAbnormalType);
        } else if (current->m_eDebuff == DB_GOOD) {
            if (current->m_nAbnormalTime != -2) {
                buffs.insert(current->m_nAbnormalType);
            }
        } else if (current->m_eDebuff >= DB_BAD && current->m_eDebuff <= DB_MENTAL_MES) {
            debuffs.insert(current->m_nAbnormalType);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%d] skill buff type[%d] is wrong",
            //     kCreatureServerSourceFile,
            //     0x43e8,
            //     current->m_nSkillId,
            //     static_cast<int>(current->m_eDebuff))
            // Log.Add(
            //     LOG_ERROR,
            //     "[skills] Existing skill %d has invalid buff/debuff type %d. Excluding it from buff/debuff counts and
            //     continuing.", current->m_nSkillId, static_cast<int>(current->m_eDebuff))
            L2_LOG_ERROR(
                Log,
                "[skills] Existing skill %d has invalid buff/debuff type %d; excluding it from buff/debuff counts and "
                "continuing.",
                current->m_nSkillId, static_cast<int>(current->m_eDebuff)
            );
        }
    }

    buffCount = static_cast<int>(buffs.size());
    debuffCount = static_cast<int>(debuffs.size());
    songDanceCount = static_cast<int>(songs.size());
    triggerCount = static_cast<int>(triggers.size());
    return sameIndex;
}

bool CCreature::_FindReplacableBuffAndRemove(
    CCreature* const attacker, const CSkillInfo* const skillInfo, bool& needSendVisualEffect, int replaceIndex
) {
    L2_GUARD_FUNCTION();

    if (replaceIndex == -1) {
        const int magicType = NormalizeSkillMagicType(skillInfo->m_nIsMagic);
        replaceIndex = _FindFirstReplacableBuffSlot(
            skillInfo->m_eDebuff, magicType == SIMT_SONGDANCE_SKILL, magicType == SIMT_TRIGGER_SKILL
        );
        if (replaceIndex == -1) {
            return false;
        }
    }

    needSendVisualEffect =
        runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(replaceIndex)).NeedSendVisualEffect();
    return _DeleteAbnormalStatus(replaceIndex, attacker, skillInfo, false, SPET_REPLACE, true);
}

bool CCreature::_InfectedSkillBy(
    CCreature* const attacker,
    const CSkillInfo* const skillInfo,
    const CSkillInfo* const parentSkillInfo,
    const int infectedTime,
    bool& needSendVisualEffect
) {
    L2_GUARD_FUNCTION();

    bool differentInstantItem = false;
    int buffCount = 0;
    int debuffCount = 0;
    int songDanceCount = 0;
    int triggerCount = 0;
    const int sameIndex =
        _FindSameBuff(skillInfo, differentInstantItem, buffCount, debuffCount, songDanceCount, triggerCount, attacker);

    if (sameIndex >= 0) {
        const InfectedSkillType& previous = runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(sameIndex));
        const CSkillInfo* const previousSkill = previous.pSkillInfo;
        const bool auraDuration = skillInfo->m_OperateType == SKILL_AURA ||
            skillInfo->m_OperateType == SKILL_AURA_ACTIVITY || skillInfo->m_OperateType == SKILL_AURA_ACTIVITY2;

        if (auraDuration) {
            if (previousSkill->m_nAbnormalLv == skillInfo->m_nAbnormalLv) {
                if (previousSkill->m_nSkillId == skillInfo->m_nSkillId) {
                    _ChangeAbnormalStatus(sameIndex, attacker, skillInfo);
                    return false;
                }
            } else if (previousSkill->m_nAbnormalLv > skillInfo->m_nAbnormalLv) {
                return false;
            }
        } else if (previousSkill->m_nAbnormalLv > skillInfo->m_nAbnormalLv ||
            (previousSkill->m_nAbnormalLv == skillInfo->m_nAbnormalLv && skillInfo->m_nAbnormalTime == -1)) {
            return false;
        }

        needSendVisualEffect = previous.NeedSendVisualEffect();
        if (!_DeleteAbnormalStatus(sameIndex, attacker, skillInfo, false, SPET_REPLACE, true)) {
            return false;
        }
    } else {
        int replaceIndex = -1;
        bool needReplacement = false;
        if (skillInfo->m_eDebuff == DB_GOOD && !differentInstantItem) {
            if (IsSummonOrPet() && !IsPet() && buffCount > 0x13) {
                CSummon* const summon = CastSummon();
                User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
                if (master != nullptr) {
                    m_SkillDurationMod.m_nBuffSlotBonus = master->m_SkillDurationMod.m_nBuffSlotBonus;
                }
            }

            const int magicType = NormalizeSkillMagicType(skillInfo->m_nIsMagic);
            bool hasRoom = false;
            if (magicType == SIMT_SONGDANCE_SKILL) {
                hasRoom = songDanceCount < 0x0c;
            } else if (magicType == SIMT_TRIGGER_SKILL) {
                hasRoom = triggerCount < 0x18;
            } else {
                const int bonus = std::min(m_SkillDurationMod.m_nBuffSlotBonus, 0x10);
                hasRoom = buffCount < bonus + 0x14;
            }

            if (!hasRoom) {
                replaceIndex =
                    _FindFirstUnsharedInstantBuff(magicType == SIMT_SONGDANCE_SKILL || magicType == SIMT_TRIGGER_SKILL);
                if (replaceIndex < 0 && skillInfo->m_bInstantItem) {
                    return false;
                }
                needReplacement = true;
            }
        } else if (skillInfo->m_eDebuff >= DB_BAD && skillInfo->m_eDebuff <= DB_MENTAL_MES && debuffCount >= 0x25) {
            needReplacement = true;
        }

        if (needReplacement && !_FindReplacableBuffAndRemove(attacker, skillInfo, needSendVisualEffect, replaceIndex)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't find replacable buff slot.")
            // Log.Add(
            //     LOG_NORMAL,
            //     "[skills] Skill %d, level %d, sublevel %d was not applied to object %u: the required buff replacement
            //     could not be selected or removed.", skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel,
            //     m_nId)
            L2_LOG_INFO(
                Log,
                "[skills] Skill %d, level %d, sublevel %d was not applied to object %u: the required buff replacement could "
                "not be selected or removed.",
                skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, m_nId
            );
            return false;
        }
    }

    const CSkillInfo* appliedSkill = skillInfo;
    if (skillInfo->m_nChangeBuffId > 0) {
        static constexpr int changeBuffLevels[] = {0, 40, 70, 84, 94, 100};
        std::uint16_t level = 1;
        const int creatureLevel = m_data->m_nLevel;
        for (std::uint16_t index = 0; index < 5; ++index) {
            if (changeBuffLevels[index] < creatureLevel && creatureLevel <= changeBuffLevels[index + 1]) {
                level = static_cast<std::uint16_t>(index + 1);
                break;
            }
        }
        const CSkillInfo* const changed = g_SkillDB.GetSkillInfo(CSkillKey(skillInfo->m_nChangeBuffId, level), nullptr);
        if (changed != nullptr) {
            appliedSkill = changed;
        }
    }

    InfectedSkillType infected = CreateInfectedSkillType(attacker, appliedSkill, parentSkillInfo, infectedTime);
    m_InfectedSkills.push_back(infected);
    needSendVisualEffect = needSendVisualEffect || infected.NeedSendVisualEffect();
    std::stable_sort(m_InfectedSkills.begin(), m_InfectedSkills.end(), InfectedSkillBuffSort{});
    _AddAbnormalStatus(*appliedSkill, attacker, true, needSendVisualEffect, SPST_CASTING);
    if (IsPCOrSummon()) {
        _CheckBuffLock();
    }
    return true;
}

bool CCreature::InfectedInstantDurationSkillBy(CCreature* const attacker, const CSkillInfo* const skillInfo) {
    L2_GUARD_FUNCTION();
    if (skillInfo->m_nAbnormalType != ABNS_HP_RECOVER) {
        return false;
    }

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    {
        L2_GUARD(m_InstantDurationSect);
        if (m_InstantDurationSkill.pSkillInfo != nullptr &&
            m_InstantDurationSkill.pSkillInfo->m_nAbnormalLv > skillInfo->m_nAbnormalLv) {
            return false;
        }

        m_InstantDurationSkill.pSkillInfo = const_cast<CSkillInfo*>(skillInfo);
        m_InstantDurationSkill.nStartTime = now;
        if (attacker == nullptr) {
            m_InstantDurationSkill.bSelfCast = false;
            m_InstantDurationSkill.nSourceCreatureId = 0;
        } else {
            m_InstantDurationSkill.bSelfCast = attacker->GetIndex() == GetIndex();
            m_InstantDurationSkill.nSourceCreatureId = attacker->GetSmartID().Sid();
        }
    }

    {
        L2_GUARD(m_InfectedSkillsSect);
        _AddAbnormalStatus(*skillInfo, attacker, true, true, SPST_CASTING);
    }

    if (IsPC()) {
        CastUser()->SendShortBuffStatusInfo();
        const int abnormalTime = GetAbnormalTime(skillInfo, false);
        skillInfo->SendRegenPacket(CastUser(), abnormalTime);
    }
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    } else {
        ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    }
    if (!skillInfo->m_AbnormalVisualEffect.empty()) {
        CheckAbnormalVisualEffect();
    }
    return true;
}

bool CCreature::InfectedSkillBy(
    CCreature* const attacker,
    const CSkillInfo* const skillInfo,
    const CSkillInfo* const parentSkillInfo,
    const int infectedTime
) {
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return false;
    }

    if (skillInfo->m_OperateType == SKILL_AURA_ACTIVITY) {
        L2_GUARD(m_SkillMod.m_Lock);
        const auto aura = m_SkillMod.m_SkillAuraMap.find(const_cast<CSkillInfo*>(skillInfo));
        if (aura != m_SkillMod.m_SkillAuraMap.end() && aura->second.m_bOn &&
            aura->second.m_nSkillId == skillInfo->m_nSkillId) {
            return false;
        }
    }

    const int skillId = skillInfo->m_nSkillId;
    const bool specialLevelBypassesDeath = static_cast<std::uint32_t>(skillInfo->m_nSpecialLevel) + 2U < 2U;
    if (IsPCOrSummon() && !IsAlive() && !specialLevelBypassesDeath && skillId != g_nClanAdventSkill && skillId != 0x38eb &&
        skillId != 0x161c && skillId != 0x5b30 && skillId != 0x1b60 && skillId != 0x1bcb) {
        return false;
    }

    if (skillInfo->m_nAbnormalType == ABNS_HP_RECOVER) {
        return InfectedInstantDurationSkillBy(attacker, skillInfo);
    }
    if (!CanBeBuffed(skillInfo, attacker)) {
        return false;
    }

    bool needSendVisualEffect = false;
    bool infected = false;
    {
        L2_GUARD(m_InfectedSkillsSect);
        infected = _InfectedSkillBy(attacker, skillInfo, parentSkillInfo, infectedTime, needSendVisualEffect);
    }
    if (!infected) {
        return false;
    }

    if (skillInfo->m_eDebuff == DB_PHYSICAL_MES) {
        InfectedMes(MesType::PHYSICAL, true);
    } else if (skillInfo->m_eDebuff == DB_MENTAL_MES) {
        InfectedMes(MesType::MENTAL, true);
    }
    return true;
}

bool CCreature::AttachedAbnormalStatusByChannelling(
    CCreature* const attacker, CSkillInfo* const skillInfo, CObject* const target
) {
    // Current L2Server 0x006295d4.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    const double probability = skillInfo->GetDurationSkillProb(attacker, targetCreature);
    if (genrand64_real1() * 100.0 < probability || attacker->m_AlwaysSkillSuccess) {
        CSkillInfo* const attachedSkill = skillInfo->m_pAttachedSkill;
        if (attachedSkill == nullptr) {
            return false;
        }

        {
            L2_GUARD(m_InfectedSkillsSect);
            for (InfectedSkillType& infected : m_InfectedSkills) {
                if (infected.pSkillInfo == nullptr || infected.pSkillInfo->m_nSkillId != attachedSkill->m_nSkillId ||
                    infected.m_pChannellingSkillInfo == nullptr ||
                    infected.m_pChannellingSkillInfo->m_OperateType != skillInfo->m_OperateType) {
                    continue;
                }
                if (infected.pSkillInfo->m_pNextLevel == nullptr) {
                    return false;
                }

                infected.pSkillInfo = infected.pSkillInfo->m_pNextLevel;
                infected.m_SourceCreatureIds.push_back(attacker->m_nId);
                m_data->SetAbnormalLevel(
                    infected.pSkillInfo->m_nAbnormalType, static_cast<unsigned char>(infected.pSkillInfo->m_nLevel)
                );
                _OnAbnormalStatusChanged(*infected.pSkillInfo, attacker, SPST_CHANNELING_START, true);
                return true;
            }
        }
        return InfectedSkillBy(attacker, attachedSkill, skillInfo, 0);
    }

    if (targetCreature->IsAlive()) {
        User* const owner = attacker->GetUserOrMaster();
        if (owner != nullptr) {
            const int encodedLevel = skillInfo->m_nSubLevel << 16 | static_cast<std::uint16_t>(skillInfo->m_nLevel);
            if (targetCreature->IsNPC()) {
                owner->GetSocket()->Send(
                    "chccdcdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 2, targetCreature->GetClassId(),
                    4, skillInfo->m_nSkillId, encodedLevel
                );
            } else if (targetCreature->IsPC()) {
                User* const targetUser = targetCreature->CastUser();
                owner->GetSocket()->Send(
                    "chccScdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 12, targetUser->GetName2(owner),
                    4, skillInfo->m_nSkillId, encodedLevel
                );
            }
        }
    }
    attacker->GetController()->DoNothing(true);
    attacker->StopMove();
    return false;
}

bool CCreature::DeleteChannelingAbnormalStatus(
    CCreature* const source, const CSkillInfo* const skillInfo, const bool notifyChange, const SkillPumpEndType pumpEndType
) {
    // Current L2Server 0x0061dd9c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    if (skillInfo == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
        if (infected.pSkillInfo == nullptr || infected.pSkillInfo->m_nAbnormalType != skillInfo->m_nAbnormalType) {
            continue;
        }

        const InfectedSkillType deletedSkill(infected);
        const CSkillInfo* const deletedSkillInfo = deletedSkill.pSkillInfo;
        const AbnormalStatusSlotType abnormalType = deletedSkillInfo->m_nAbnormalType;
        m_InfectedSkills.erase(m_InfectedSkills.begin() + static_cast<std::ptrdiff_t>(index));
        m_data->DeleteAbnormalStatus(abnormalType);

        if (IsPCOrSummon() && notifyChange) {
            _CheckBuffLock();
        }
        _OnAbnormalStatusDeleted(*deletedSkillInfo, source, pumpEndType, static_cast<int>(deletedSkill.nSourceCreatureId));

        if (!IsPC()) {
            SetSpelledInfoChanged();
        } else if (User* const user = CastUser(); user != nullptr) {
            user->m_bSpelledInfoChanged = true;
            user->m_NeedUpdatePacketBits |= 2U;
        }
        CheckAbnormalVisualEffect();
        break;
    }
    return true;
}

bool CCreature::DetachedAbnormalStatusByChannelling(CCreature* const source, CSkillInfo* const skillInfo) {
    // Current L2Server 0x00622450.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr || skillInfo->m_pAttachedSkill == nullptr) {
        return false;
    }

    bool changed = false;
    const CSkillInfo* changedSkill = nullptr;
    int changedSkillId = 0;
    int changedSkillLevel = 0;
    int changedSkillSubLevel = 0;
    {
        L2_GUARD(m_InfectedSkillsSect);
        CSkillInfo* const attachedSkill = skillInfo->m_pAttachedSkill;

        if (skillInfo->m_OperateType == SKILL_CHANNELLING_DURATION) {
            if (_DeleteAbnormalStatus(attachedSkill->m_nAbnormalType, source, skillInfo, true, SPET_CHANNELING_END, false)) {
                changedSkill = attachedSkill;
                changedSkillId = attachedSkill->m_nSkillId;
                changedSkillLevel = attachedSkill->m_nLevel;
                changedSkillSubLevel = attachedSkill->m_nSubLevel;
                changed = true;
            }
        } else if (skillInfo->m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP) {
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
                CSkillInfo* const currentSkill = infected.pSkillInfo;
                if (currentSkill == nullptr || currentSkill->m_nSkillId != attachedSkill->m_nSkillId ||
                    infected.m_pChannellingSkillInfo == nullptr ||
                    infected.m_pChannellingSkillInfo->m_OperateType != SKILL_CHANNELLING_DURATION_LEVEL_UP) {
                    continue;
                }

                changedSkill = currentSkill;
                changedSkillId = currentSkill->m_nSkillId;
                changedSkillLevel = currentSkill->m_nLevel;
                changedSkillSubLevel = currentSkill->m_nSubLevel;
                if (currentSkill->m_pPrevLevel == nullptr) {
                    _DeleteAbnormalStatus(
                        currentSkill->m_nAbnormalType, source, skillInfo, true, SPET_CHANNELING_END, false
                    );
                } else {
                    infected.pSkillInfo = currentSkill->m_pPrevLevel;
                    if (source != nullptr) {
                        const auto sourceIt = std::find(
                            infected.m_SourceCreatureIds.begin(), infected.m_SourceCreatureIds.end(),
                            static_cast<int>(source->m_nId)
                        );
                        if (sourceIt != infected.m_SourceCreatureIds.end()) {
                            infected.m_SourceCreatureIds.erase(sourceIt);
                        }
                    }
                    m_data->SetAbnormalLevel(
                        infected.pSkillInfo->m_nAbnormalType, static_cast<unsigned char>(infected.pSkillInfo->m_nLevel)
                    );
                    _OnAbnormalStatusChanged(*infected.pSkillInfo, source, SPST_CHANNELING_END, true);
                }
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        std::vector<const CSkillInfo*> changedSkills;
        changedSkills.push_back(changedSkill);
        PostJobAfterDispel(changedSkills, changedSkillId, changedSkillLevel, changedSkillSubLevel);
    }
    return changed;
}

int CCreature::AuraSkillBySkill() {
    // Current L2Server.exe 0x0061499c.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillAuraMap auras;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillAuraMap.empty()) {
            return 0;
        }
        auras = m_SkillMod.m_SkillAuraMap;
    }

    int activatedCount = 0;
    for (const auto& entry : auras) {
        const CCreatureSkillMod::SkillAuraInfo& aura = entry.second;
        if (!aura.m_bOn) {
            continue;
        }
        PushSerializedMessage(new CSerializedAuraSkillMessage(this, *entry.first, aura.m_eTargetType));
        ++activatedCount;
    }
    return activatedCount;
}

int CCreature::_SynergyAuraByAbnormalSlot() {
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillSynergyAuraMap synergyAuras;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillSynergyAuraMap.empty()) {
            return 0;
        }
        synergyAuras = m_SkillMod.m_SkillSynergyAuraMap;
    }

    int activatedCount = 0;
    for (const auto& entry : synergyAuras) {
        const CCreatureSkillMod::SkillSynergyAuraInfo& aura = entry.second;
        CSkillInfo* const skillInfo = m_SkillMod.GetSynergySkillInfo(this, aura.m_nSkillSynergyId);
        if (skillInfo == nullptr) {
            continue;
        }
        PushSerializedMessage(new CSerializedAuraSkillMessage(this, *skillInfo, aura.m_eTargetType));
        ++activatedCount;
    }
    return activatedCount;
}

bool CCreature::_DeleteAbnormalStatus(
    const int index,
    CCreature* source,
    const CSkillInfo* skillInfo,
    bool notifyChange,
    SkillPumpEndType pumpEndType,
    bool unknown
) {
    L2_GUARD_FUNCTION();
    (void)unknown;

    if (static_cast<std::size_t>(index) >= m_InfectedSkills.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Try to delete abnormal status pass-by-index. [%d] [%d]",
        //     static_cast<int>(m_InfectedSkills.size()),
        //     index)
        // Log.Add(
        //     LOG_ERROR,
        //     "[skills] Abnormal-effect removal rejected for object %u: index %d is outside the list of %zu entries.",
        //     m_nId, index, m_InfectedSkills.size())
        L2_LOG_ERROR(
            Log, "[skills] Abnormal-effect removal rejected for object %u: index %d is outside the list of %zu entries.",
            m_nId, index, m_InfectedSkills.size()
        );
        return false;
    }

    const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index));
    const int abnormalTime =
        infected.pSkillInfo->m_nAbnormalTime == -2 ? infected.nDuration : GetAbnormalTime(infected.pSkillInfo, false);
    const int now = runtime::crt::Time32Now();
    const int remainingTime =
        runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(abnormalTime, now), infected.nStartTime);
    if (pumpEndType == SPET_TIMEOUT && remainingTime > 0) {
        return false;
    }

    const InfectedSkillType deletedSkill(infected);
    const CSkillInfo* const deletedSkillInfo = deletedSkill.pSkillInfo;
    const AbnormalStatusSlotType abnormalType = deletedSkillInfo->m_nAbnormalType;
    if (deletedSkillInfo->m_nIsMagic == SIMT_SONGDANCE_SKILL) {
        m_nSongDanceCount = runtime::AddInt32Wrapping(m_nSongDanceCount, -1);
        if (m_nSongDanceCount < 0) {
            m_nSongDanceCount = 0;
        } else if (m_nSongDanceCount > 0x13) {
            m_nSongDanceCount = 0x13;
        }
    }

    InfectedSkillType::SkillActors skillActors;
    if (deletedSkill.IsChannellingSkill()) {
        const bool sourceOwnsChannel = source != nullptr &&
            std::find(
                deletedSkill.m_SourceCreatureIds.begin(), deletedSkill.m_SourceCreatureIds.end(),
                static_cast<int>(source->m_nId)
            ) != deletedSkill.m_SourceCreatureIds.end();
        if (!sourceOwnsChannel) {
            for (const int actorId : deletedSkill.m_SourceCreatureIds) {
                skillActors.push_back(actorId);
            }
        }
    }

    if (g_bUseNPLogin && deletedSkillInfo->m_nAbnormalTime == -2 && IsPC()) {
        GetSocket()->PopGradeSkill(deletedSkillInfo);
    }

    m_InfectedSkills.erase(m_InfectedSkills.begin() + index);
    m_data->DeleteAbnormalStatus(abnormalType);

    if ((abnormalType == ABNS_PRESERVE_ABNORMAL || abnormalType == ABNS_REDUCE_DROP_PENALTY ||
            abnormalType == ABNS_RESURRECTION_SPECIAL) &&
        IsPC()) {
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x1b5, this, nullptr, nullptr, nullptr, nullptr, deletedSkillInfo->m_nSkillId, deletedSkillInfo->m_nLevel, 0, 0,
            0, 0, 0
        );
    }

    if (IsPC() && deletedSkillInfo->m_nSpecialLevel < 0 && deletedSkillInfo->m_nSpecialLevel > -5 &&
        deletedSkillInfo->m_IrreplaceableBuff && skillInfo != nullptr &&
        (skillInfo->m_OperateType == SKILL_DURATION || skillInfo->m_OperateType == SKILL_CHARGE_DURATION ||
            skillInfo->m_OperateType == SKILL_SELFDURATION)) {
        const std::uint32_t levelBits = (static_cast<std::uint32_t>(deletedSkillInfo->m_nSubLevel) << 16) |
            static_cast<std::uint16_t>(deletedSkillInfo->m_nLevel);
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1ad, CastUser(), nullptr, deletedSkillInfo->m_nSkillId, runtime::Int32FromBits(levelBits),
            static_cast<int>(pumpEndType), remainingTime, 0, 0, 0
        );
    }

    if (IsPCOrSummon() && notifyChange) {
        _CheckBuffLock();
    }

    _OnAbnormalStatusDeleted(*deletedSkillInfo, source, pumpEndType, static_cast<int>(deletedSkill.nSourceCreatureId));

    if (IsPC()) {
        User* const user = CastUser();
        if (user != nullptr) {
            L2_GUARD(user->m_ModifySkillLock);
            const int sourceId = static_cast<int>(deletedSkill.nSourceCreatureId);
            const std::CTriKey<AbnormalStatusSlotType, int, int> key(abnormalType, deletedSkillInfo->m_nSkillId, sourceId);
            const User::ModifySkillList value{
                deletedSkillInfo->m_nSkillId, deletedSkillInfo->m_nLevel, deletedSkillInfo->m_nSubLevel, sourceId};
            user->m_DeleteSkill.emplace(key, value);
        }
    }

    if (!IsPC()) {
        SetSpelledInfoChanged();
    } else {
        User* const user = CastUser();
        if (user != nullptr) {
            user->m_bSpelledInfoChanged = true;
            user->m_NeedUpdatePacketBits |= 2U;
            if (user->IsAlreadyHaveMentor() && user->m_Mentee.CanBeBuffed()) {
                runtime::raw_atomic::ExchangeI32(&user->m_Mentee.m_nInfectedSkillByMentor, 0);
            }
        }
    }

    _CheckAbnormalVisualEffect();
    for (int& actorId : skillActors) {
        CreatureCommonFunc::DoNothingSkillActors(actorId);
    }
    return true;
}

bool CCreature::DispelByNames(
    const std::vector<wchar_t*>& skillNames, CCreature* const source, CSkillInfo* const skillInfo
) {
    // Current L2Server 0x00621a5c.
    L2_GUARD_FUNCTION();

    bool removed = false;
    std::vector<const CSkillInfo*> removedSkills;
    for (wchar_t* const skillName : skillNames) {
        if (std::wcscmp(skillName, L"none") == 0) {
            // Noise: The literal skill name none is a supported skip marker in this branch. No removal is attempted.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] - Get skill name[%s]",
            //     "Creature_server.cpp",
            //     0x3fd1,
            //     skillName)
            continue;
        }

        // The shipping body ignores the lookup result. Valid
        // skill data initializes all three values. Failed lookups keep the
        // original stack-state behavior.
        int resolvedSkillId;
        int resolvedSkillLevel;
        int resolvedSkillSubLevel;
        g_SkillDB.GetSkillIdLevelByName(skillName, &resolvedSkillId, &resolvedSkillLevel, &resolvedSkillSubLevel);

        L2_GUARD(m_InfectedSkillsSect);
        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                CSkillInfo* const infectedSkill = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (static_cast<std::uint32_t>(infectedSkill->m_nSpecialLevel) + 2U < 2U ||
                    infectedSkill->m_nSkillId != resolvedSkillId || resolvedSkillLevel < infectedSkill->m_nLevel) {
                    continue;
                }

                removedSkills.push_back(infectedSkill);
                removed = true;
                _DeleteAbnormalStatus(static_cast<int>(index), source, infectedSkill, true, SPET_DISPEL, true);
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (removed) {
        PostJobAfterDispel(removedSkills, skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
    }
    return removed;
}

int CCreature::DispelBySlot(
    const AbnormalStatusSlotType abnormalType,
    CCreature* const source,
    const int maximumAbnormalLevel,
    const CSkillInfo* const skillInfo,
    const SkillPumpEndType pumpEndType,
    const bool sameCasterOnly
) {
    L2_GUARD_FUNCTION();

    std::vector<const CSkillInfo*> deletedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);

        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
                CSkillInfo* const infectedSkill = infected.pSkillInfo;
                if (infectedSkill == nullptr || infectedSkill->m_nAbnormalType != abnormalType) {
                    continue;
                }
                if (maximumAbnormalLevel != -1 && static_cast<std::uint32_t>(infectedSkill->m_nSpecialLevel + 2) <= 1U) {
                    continue;
                }
                if (sameCasterOnly && source != nullptr && infected.nSourceCreatureId != source->GetSmartID().Sid()) {
                    continue;
                }
                if (maximumAbnormalLevel != -1 && maximumAbnormalLevel < infectedSkill->m_nAbnormalLv) {
                    continue;
                }

                deletedSkills.push_back(infectedSkill);
                _DeleteAbnormalStatus(static_cast<int>(index), source, infectedSkill, true, pumpEndType, true);
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (!deletedSkills.empty()) {
        const int dispelSkillId = skillInfo != nullptr ? skillInfo->m_nSkillId : 0;
        const int dispelSkillLevel = skillInfo != nullptr ? skillInfo->m_nLevel : 0;
        const int dispelSkillSubLevel = skillInfo != nullptr ? skillInfo->m_nSubLevel : 0;
        PostJobAfterDispel(deletedSkills, dispelSkillId, dispelSkillLevel, dispelSkillSubLevel);
    }
    return static_cast<int>(deletedSkills.size());
}

bool CCreature::DispelBySlotProb(
    const AbnormalStatusSlotType abnormalType, CCreature* const source, const int probability, CSkillInfo* const skillInfo
) {
    // Current L2Server 0x00620ad4.
    L2_GUARD_FUNCTION();

    bool removed = false;
    std::vector<const CSkillInfo*> removedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);

        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                CSkillInfo* const infectedSkill = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (infectedSkill == nullptr || infectedSkill->m_nAbnormalType != abnormalType ||
                    static_cast<std::uint32_t>(infectedSkill->m_nSpecialLevel) + 2U <= 1U) {
                    continue;
                }

                const double dispelProbability = std::clamp(
                    CreatureCommonFunc::GetDispelProbability(
                        static_cast<double>(probability), skillInfo, infectedSkill, m_SkillMod, Level()
                    ),
                    10.0, 90.0
                );
                if (::genrand64_real1() * 100.0 >= dispelProbability) {
                    continue;
                }

                removedSkills.push_back(infectedSkill);
                removed = true;
                _DeleteAbnormalStatus(static_cast<int>(index), source, skillInfo, true, SPET_DISPEL, true);
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (removed) {
        PostJobAfterDispel(removedSkills, skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
    }
    return removed;
}

bool CCreature::DispelByIdLevelMySelf(const int skillId, const int skillLevel, const int skillSubLevel) {
    // Current L2Server 0x0062697c, original owner Creature_server.cpp.
    L2_GUARD_FUNCTION();

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), this);
    return DispelByIdLevelMySelf(skillInfo, false);
}

bool CCreature::DispelByIdLevelMySelf(const CSkillInfo* const skillInfo, const bool force) {
    // Current L2Server 0x00620d98, original owner Creature_server.cpp.
    L2_GUARD_FUNCTION();

    if (skillInfo == nullptr ||
        (!force &&
            (skillInfo->m_eDebuff != DB_GOOD || static_cast<std::uint32_t>(skillInfo->m_nSpecialLevel) + 2U <= 1U ||
                skillInfo->m_nIsMagic == SIMT_SONGDANCE_SKILL))) {
        return false;
    }

    bool removed = false;
    std::vector<const CSkillInfo*> removedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);
        for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
            const CSkillInfo* const infectedSkill = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
            if (infectedSkill != skillInfo) {
                continue;
            }

            removedSkills.push_back(infectedSkill);
            removed = true;
            _DeleteAbnormalStatus(static_cast<int>(index), this, infectedSkill, true, SPET_SELF_DISPEL, true);
            break;
        }
    }

    if (removed) {
        PostJobAfterDispel(removedSkills, 0, 0, 0);
    }

    if (IsPC()) {
        User* const user = CastUser();
        const FVector position = user->GetPos();
        const std::wstring accountName = Utf16ToWide(std::u16string_view(user->m_sAccountName.data()));
        const int packedSkillLevel = static_cast<int>(
            (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skillInfo->m_nLevel)
        );
        // Match the native variadic argument widths. Preserve the accounting CSV schema.
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        //     0x1ab,
        //     user->GetDBID(),
        //     user->GetAccountID(),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     user->m_data->m_Race,
        //     user->m_data->m_Sex,
        //     user->m_data->m_nClass,
        //     user->m_data->m_nLevel,
        //     skillInfo->m_nSkillId,
        //     packedSkillLevel,
        //     user->GetName(),
        //     accountName.c_str())
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        //     static_cast<long long>(0x1ab),
        //     static_cast<long long>(user->GetDBID()),
        //     static_cast<long long>(user->GetAccountID()),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<long long>(user->m_data->m_Race),
        //     static_cast<long long>(user->m_data->m_Sex),
        //     static_cast<long long>(user->m_data->m_nClass),
        //     static_cast<long long>(user->m_data->m_nLevel),
        //     static_cast<long long>(skillInfo->m_nSkillId),
        //     static_cast<long long>(packedSkillLevel),
        //     user->GetName(),
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
            static_cast<long long>(0x1ab), static_cast<long long>(user->GetDBID()),
            static_cast<long long>(user->GetAccountID()),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<long long>(user->m_data->m_Race), static_cast<long long>(user->m_data->m_Sex),
            static_cast<long long>(user->m_data->m_nClass), static_cast<long long>(user->m_data->m_nLevel),
            static_cast<long long>(skillInfo->m_nSkillId), static_cast<long long>(packedSkillLevel), user->GetName(),
            accountName.c_str()
        );
    }
    return removed;
}

bool CCreature::DispelByCategory(
    const DebuffType debuffType,
    CCreature* const source,
    const int probability,
    const CSkillInfo* const skillInfo,
    const int maximumSlots
) {
    // Current L2Server 0x006212bc.
    L2_GUARD_FUNCTION();

    bool removed = false;
    int removedCount = 0;
    std::vector<const CSkillInfo*> removedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);

        for (int index = static_cast<int>(m_InfectedSkills.size()) - 1; index >= 0; --index) {
            const CSkillInfo* const infectedSkill =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
            if (infectedSkill == nullptr) {
                continue;
            }
            if (debuffType != DB_MAX &&
                ((debuffType == DB_GOOD && infectedSkill->m_eDebuff != DB_GOOD) ||
                    (debuffType == DB_BAD && infectedSkill->m_eDebuff == DB_GOOD))) {
                continue;
            }
            if ((infectedSkill->m_nSpecialLevel < 0 && infectedSkill->m_nSpecialLevel >= -4) ||
                infectedSkill->m_nBuffProtectLevel >= 2) {
                continue;
            }

            double adjustedProbability = 100.0;
            if (probability != 100) {
                adjustedProbability = std::clamp(
                    CreatureCommonFunc::GetDispelProbability(
                        static_cast<double>(probability), skillInfo, infectedSkill, m_SkillMod, Level()
                    ),
                    0.0, 50.0
                );
            }
            if (::genrand64_real1() * 100.0 >= adjustedProbability) {
                continue;
            }

            removedSkills.push_back(infectedSkill);
            removed = true;
            _DeleteAbnormalStatus(index, source, infectedSkill, true, SPET_DISPEL, true);
            ++removedCount;
            if (maximumSlots <= removedCount) {
                break;
            }
        }
    }

    if (removed) {
        PostJobAfterDispel(
            removedSkills, skillInfo != nullptr ? skillInfo->m_nSkillId : 0, skillInfo != nullptr ? skillInfo->m_nLevel : 0,
            skillInfo != nullptr ? skillInfo->m_nSubLevel : 0
        );
    }
    return removed;
}

bool CCreature::DispelAll(const int magicLevel, CCreature* source, const CSkillInfo* skillInfo) {
    // Current L2Server 0x006215e0.
    L2_GUARD_FUNCTION();
    bool removed = false;
    std::vector<const CSkillInfo*> removedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);
        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                const CSkillInfo* const infectedSkill = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (infectedSkill == nullptr) {
                    continue;
                }

                bool removeThis = true;
                if (static_cast<unsigned int>(infectedSkill->m_nSpecialLevel + 2) > 1U) {
                    const int probability = std::max((magicLevel - infectedSkill->m_nMagicLevel) * 2 + 75, 20);
                    removeThis = ::genrand64_real1() * 100.0 + 0.0 < static_cast<double>(probability);
                }
                if (!removeThis) {
                    continue;
                }

                removedSkills.push_back(infectedSkill);
                _DeleteAbnormalStatus(static_cast<int>(index), source, infectedSkill, true, SPET_DISPEL, true);
                removed = true;
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (removed) {
        PostJobAfterDispel(removedSkills, skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
    }
    return removed;
}

bool CCreature::DispelAllByGM(SkillPumpEndType pumpEndType, bool goodOnly) {
    // Current L2Server 0x00621838.
    L2_GUARD_FUNCTION();
    bool removed = false;
    std::vector<const CSkillInfo*> removedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);
        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                const CSkillInfo* const skillInfo = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (skillInfo == nullptr || (skillInfo->m_nSpecialLevel == -2 && pumpEndType == SPET_DIE)) {
                    continue;
                }

                SkillPumpEndType effectiveEndType = pumpEndType;
                bool removeThis = false;
                if (goodOnly) {
                    if (skillInfo->m_eDebuff == DB_GOOD) {
                        removeThis = true;
                        if (effectiveEndType == SPET_NONE) {
                            effectiveEndType = SPET_DISPEL;
                        }
                    } else {
                        removed = true;
                    }
                } else {
                    removed = true;
                    removeThis = pumpEndType != SPET_DUEL_END || !skillInfo->m_bExceptRestore;
                }
                if (!removeThis) {
                    continue;
                }

                removedSkills.push_back(skillInfo);
                _DeleteAbnormalStatus(static_cast<int>(index), this, skillInfo, true, effectiveEndType, true);
                removed = true;
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (removed) {
        PostJobAfterDispel(removedSkills, 0, 0, 0);
    }
    return removed;
}

bool CCreature::DispelByBuffProtectionFlag(const unsigned long protectionFlag) {
    L2_GUARD_FUNCTION();

    bool changed = false;
    std::vector<const CSkillInfo*> deletedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);

        for (;;) {
            bool deleted = false;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                CSkillInfo* const skillInfo = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (skillInfo == nullptr ||
                    (static_cast<unsigned long>(skillInfo->m_nBuffProtectLevel) & protectionFlag) != 0) {
                    continue;
                }

                if ((protectionFlag & 0x20UL) != 0 && skillInfo->m_eDebuff == DB_BAD) {
                    changed = true;
                    continue;
                }

                deletedSkills.push_back(skillInfo);
                changed = true;
                _DeleteAbnormalStatus(static_cast<int>(index), this, skillInfo, true, SPET_DISPEL, true);
                deleted = true;
                break;
            }
            if (!deleted) {
                break;
            }
        }
    }

    if (changed) {
        PostJobAfterDispel(deletedSkills, 0, 0, 0);
    }
    return changed;
}

bool CCreature::DetachAllChannelling() {
    L2_GUARD_FUNCTION();

    bool changed = false;
    std::vector<const CSkillInfo*> deletedSkills;
    for (std::size_t index = 0; index < m_InfectedSkills.size();) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, index);
        if (!infected.IsChannellingSkill()) {
            ++index;
            continue;
        }

        const CSkillInfo* const skillInfo = infected.pSkillInfo;
        if (_DeleteAbnormalStatus(static_cast<int>(index), nullptr, skillInfo, true, SPET_CHANNELING_END, true)) {
            deletedSkills.push_back(skillInfo);
            changed = true;
        } else {
            ++index;
        }
    }

    if (changed) {
        PostJobAfterDispel(deletedSkills, 0, 0, 0);
    }
    return true;
}

void CCreature::PostJobAfterDispel(
    const std::vector<const CSkillInfo*>& skills, const int skillId, const int skillLevel, const int skillSubLevel
) {
    L2_GUARD_FUNCTION();

    if (IsPC()) {
        User* const user = CastUser();
        m_bAbnormalStatusChanged = true;
        CLogFormatter formatter;
        for (const CSkillInfo* const removedSkill : skills) {
            if (removedSkill == nullptr) {
                continue;
            }
            formatter.WriteLog2_1(
                0x196, user, nullptr, skillId,
                (static_cast<std::int64_t>(skillSubLevel) << 16) | static_cast<std::uint16_t>(skillLevel),
                removedSkill->m_nSkillId,
                (static_cast<std::int64_t>(removedSkill->m_nSubLevel) << 16) |
                    static_cast<std::uint16_t>(removedSkill->m_nLevel),
                0, 0, 0
            );
        }
    }
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    } else {
        ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    }
    CheckAbnormalVisualEffect();
}

bool CCreature::_DeleteAbnormalStatus(
    AbnormalStatusSlotType abnormalType,
    CCreature* source,
    CSkillInfo* skillInfo,
    bool notifyChange,
    SkillPumpEndType pumpEndType,
    bool unknown
) {
    L2_GUARD_FUNCTION();
    (void)unknown;

    const int infectedSkillCount = static_cast<int>(m_InfectedSkills.size());
    for (int index = 0; index < infectedSkillCount; ++index) {
        const InfectedSkillType& infected = runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index));
        if (infected.pSkillInfo->m_nAbnormalType != abnormalType) {
            continue;
        }

        if (abnormalType == ABNS_STUN) {
            if (CIOObject::IsInTime(infected.nStartTime, 1)) {
                return false;
            }
            int probability = 0x0e;
            if (IsPC() && source != nullptr && source->IsPC()) {
                probability = 0x23;
            }
            if (static_cast<double>(probability) < ::genrand64_real1() * 100.0 + 0.0) {
                return false;
            }
        } else if (abnormalType == ABNS_REAL_TARGET) {
            const int probability = runtime::Win64CvttDoubleToInt32(m_SkillMod.m_dRealTargetDispelProb);
            if (static_cast<double>(probability) < ::genrand64_real1() * 100.0 + 0.0) {
                return false;
            }
        }

        return _DeleteAbnormalStatus(index, source, skillInfo, notifyChange, pumpEndType, true);
    }
    return false;
}

void CCreature::_AddAbnormalStatus(
    const CSkillInfo& skillInfo, CCreature* source, bool notifyChange, bool unknown, SkillPumpStartType startType
) {
    L2_GUARD_FUNCTION();

    if (skillInfo.m_nIsMagic == SIMT_SONGDANCE_SKILL) {
        m_nSongDanceCount = runtime::AddInt32Wrapping(m_nSongDanceCount, 1);
        if (m_nSongDanceCount < 0) {
            m_nSongDanceCount = 0;
        } else if (m_nSongDanceCount > 0x13) {
            m_nSongDanceCount = 0x13;
        }
    }

    m_data->AddAbnormalStatus(
        skillInfo.m_nAbnormalType, static_cast<std::uint8_t>(skillInfo.m_nAbnormalLv), skillInfo.m_nMagicLevel
    );

    if (source != nullptr) {
        _ChangeSkillByAbnormalSlot(source, *this);
        _TriggerSkillByAbnormalSlot(this, *source);
    }
    _SynergyAuraByAbnormalSlot();

    if (notifyChange) {
        _OnAbnormalStatusChanged(skillInfo, source, startType, unknown);
    }

    if (!IsPC()) {
        return;
    }
    User* const user = CastUser();
    if (user == nullptr) {
        return;
    }

    L2_GUARD(user->m_ModifySkillLock);
    std::int32_t sourceId = 0;
    if (skillInfo.m_nSubAbnormal >= 0 && skillInfo.m_nSubAbnormal < ABNS_MAX && source != nullptr) {
        sourceId = static_cast<std::int32_t>(source->m_nId);
    }

    const std::CTriKey<AbnormalStatusSlotType, int, int> key(skillInfo.m_nAbnormalType, skillInfo.m_nSkillId, sourceId);
    const User::ModifySkillList value{skillInfo.m_nSkillId, skillInfo.m_nLevel, skillInfo.m_nSubLevel, sourceId};
    user->m_InsertSkill.emplace(key, value);
}

void CCreature::LoadAbnormalData(unsigned long count, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    L2_GUARD(m_InfectedSkillsSect);
    m_InfectedSkills.clear();

    User* const user = GetUserOrMaster();
    const int dispelBuffLevel = user != nullptr ? user->GetDispelBuffLevel() : 0;
    const std::uint32_t recordCount = static_cast<std::uint32_t>(count);
    for (std::uint32_t record = 0; record < recordCount; ++record) {
        int skillId = 0;
        int level = 0;
        int subLevel = 0;
        int remainingTime = 0;
        int selfCast = 0;
        packet = ::Disassemble(packet, "ddddd", &skillId, &level, &subLevel, &remainingTime, &selfCast);
        if (level == 0) {
            continue;
        }

        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), this);
        if (skillInfo == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "no skill. [%d][%d][%d]",
            //     skillId,
            //     level,
            //     subLevel)
            // Log.Add(
            //     LOG_ERROR,
            //     "[skills] Skipping stored abnormal effect for object %u: skill %d, level %d, sublevel %d has no
            //     definition.", m_nId, skillId, level, subLevel)
            L2_LOG_ERROR(
                Log,
                "[skills] Skipping stored abnormal effect for object %u: skill %d, level %d, sublevel %d has no definition.",
                m_nId, skillId, level, subLevel
            );
            continue;
        }
        if (dispelBuffLevel > skillInfo->m_nBuffProtectLevel) {
            continue;
        }

        InfectedSkillType infectedSkill;
        infectedSkill.bSelfCast = selfCast == 1;
        infectedSkill.nSourceCreatureId = 0;
        infectedSkill.pSkillInfo = const_cast<CSkillInfo*>(skillInfo);
        int abnormalTime = GetAbnormalTime(skillInfo, false);
        if (abnormalTime == 0) {
            abnormalTime = IsPC() ? skillInfo->m_nPVPAbnormalTime : skillInfo->m_nPVEAbnormalTime;
        }
        infectedSkill.nStartTime = -1;
        if (skillInfo->m_nAbnormalTime == -1) {
            infectedSkill.nDuration = -1;
        } else {
            infectedSkill.nStartTime =
                runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(remainingTime, abnormalTime), now);
            infectedSkill.nDuration = remainingTime;
        }
        m_InfectedSkills.push_back(infectedSkill);

        if (IsPC() && skillInfo->m_nAbnormalType == ABNS_HP_RECOVER) {
            skillInfo->SendRegenPacket(CastUser(), remainingTime);
        }
        _AddAbnormalStatus(*skillInfo, nullptr, false, true, SPST_CASTING);
    }

    if (IsPC()) {
        if (g_bUseNPLogin) {
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->CopyGradeSkillsToInfectedSkillList();
            }
        }
        CastUser()->m_bAbnormalStatusLoaded = true;
    }
}

bool CCreature::HasReduceSkill(InfectedSkillType& infectedSkill) {
    L2_GUARD_FUNCTION();
    return infectedSkill.pSkillInfo->m_nReduceCount > 0 || infectedSkill.pSkillInfo->m_dReduceDamage > 0.0;
}

bool CCreature::IsReduceSkill(InfectedSkillType& infectedSkill) {
    L2_GUARD_FUNCTION();
    return (infectedSkill.pSkillInfo->m_nReduceCount > 0 || infectedSkill.pSkillInfo->m_dReduceDamage > 0.0) &&
        (infectedSkill.nReduceCount < 1 || infectedSkill.dReduceDamage <= 0.0);
}

bool CCreature::HasAbnormalCategory(DebuffType debuffType) {
    L2_GUARD_FUNCTION();
    if (debuffType == DB_MAX) {
        return false;
    }

    L2_GUARD(m_InfectedSkillsSect);
    const int skillCount = static_cast<int>(m_InfectedSkills.size());
    for (int index = skillCount - 1; index >= 0; --index) {
        const CSkillInfo* const skillInfo =
            runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
        if (skillInfo->m_eDebuff == debuffType && static_cast<std::uint32_t>(skillInfo->m_OperateType) < 0xfffffffeU) {
            return true;
        }
    }
    return false;
}

bool CCreature::HasAbnormalStatusSlot(::AbnormalStatusSlotType abnormalType) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    for (const InfectedSkillType& infectedSkill : m_InfectedSkills) {
        if (infectedSkill.pSkillInfo->m_nAbnormalType == abnormalType) {
            return true;
        }
    }
    return false;
}

int CCreature::_FindFirstUnsharedInstantBuff(bool shared) const {
    L2_GUARD_FUNCTION();
    if (!shared && IsPCOrSummon()) {
        const int skillCount = static_cast<int>(m_InfectedSkills.size());
        for (int current = 0; current < skillCount; ++current) {
            const CSkillInfo* const currentSkill =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(current)).pSkillInfo;
            if (!currentSkill->m_bIsDoubleCasting) {
                continue;
            }

            int compared = 0;
            for (; compared < skillCount; ++compared) {
                const CSkillInfo* const comparedSkill =
                    runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(compared)).pSkillInfo;
                if (!comparedSkill->m_bIsDoubleCasting && currentSkill->m_nAbnormalType == comparedSkill->m_nAbnormalType) {
                    break;
                }
            }
            if (compared == skillCount) {
                return current;
            }
        }
    }
    return -1;
}

int CCreature::_FindFirstReplacableBuffSlot(DebuffType debuffType, bool selfDuration, bool passive) const {
    L2_GUARD_FUNCTION();
    const int skillCount = static_cast<int>(m_InfectedSkills.size());
    for (int index = 0; index < skillCount; ++index) {
        const CSkillInfo* const skillInfo =
            runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).pSkillInfo;
        if (skillInfo->m_eDebuff != debuffType) {
            continue;
        }

        int operateType = static_cast<int>(skillInfo->m_OperateType);
        if (operateType > 9) {
            if (operateType < 14) {
                operateType = SKILL_INSTANT;
            } else if (static_cast<unsigned int>(operateType - 20) < 6U) {
                operateType = SKILL_DURATION;
            }
        }
        if (selfDuration != (operateType == SKILL_SELFDURATION)) {
            continue;
        }

        operateType = static_cast<int>(skillInfo->m_OperateType);
        if (operateType > 9) {
            if (operateType < 14) {
                operateType = SKILL_INSTANT;
            } else if (static_cast<unsigned int>(operateType - 20) < 6U) {
                operateType = SKILL_DURATION;
            }
        }
        if (passive == (operateType == SKILL_PASSIVE) && !skillInfo->m_IrreplaceableBuff) {
            return index;
        }
    }
    return -1;
}

bool CCreature::SetPassiveSkillPumpStart(int skillId, int level, int subLevel, SkillPumpStartType startType) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);

    auto found = m_PassivePumpInfo.find(skillId);
    if (found == m_PassivePumpInfo.end()) {
        if (startType == SPST_ACQUIRE_SKILL) {
            m_PassivePumpInfo.emplace(skillId, std::make_pair(std::CDualKey<int, int>(level, subLevel), true));
            return true;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Try to pump start, but NO PUMP INFO. "
        //     L"(skill:%d, %d %d) (pump_type:%d)",
        //     GetName(),
        //     skillId,
        //     level,
        //     subLevel,
        //     startType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[skills] Passive effect start rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (start
        //     type %d).", GetName(), skillId, level, subLevel, static_cast<int>(startType))
        // L2_LOG_ERROR(Log,
        //     L"[skills] Passive effect start rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (start
        //     type %d).", GetName(), skillId, level, subLevel, static_cast<int>(startType))
        L2_LOG_ERROR(
            Log,
            u"[skills] Passive effect start rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (start "
            u"type %d).",
            GetName(), skillId, level, subLevel, static_cast<int>(startType)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    auto& pumpInfo = found->second;
    if (pumpInfo.first.m_PrimaryKey == level && pumpInfo.first.m_SecondaryKey == subLevel) {
        if (!pumpInfo.second) {
            pumpInfo.second = true;
            return true;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Try to duplicated pump start. "
        //     L"(skill:%d, %d, %d) (pump_type:%d)",
        //     GetName(),
        //     skillId,
        //     level,
        //     subLevel,
        //     startType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[skills] Passive effect start ignored for %s: skill %d, level %d, sublevel %d is already active (start type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(startType))
        // L2_LOG_ERROR(Log,
        //     L"[skills] Passive effect start ignored for %s: skill %d, level %d, sublevel %d is already active (start type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(startType))
        L2_LOG_ERROR(
            Log,
            u"[skills] Passive effect start ignored for %s: skill %d, level %d, sublevel %d is already active (start type "
            u"%d).",
            GetName(), skillId, level, subLevel, static_cast<int>(startType)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    if (startType == SPST_ACQUIRE_SKILL) {
        pumpInfo.first.m_PrimaryKey = level;
        pumpInfo.first.m_SecondaryKey = subLevel;
        pumpInfo.second = true;
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s] Try to pump start with invalid pump type. "
    //     L"(skill:%d, %d, %d) (org_level:%d) (pump_type:%d)",
    //     GetName(),
    //     skillId,
    //     level,
    //     pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey,
    //     startType)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[skills] Passive effect start rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked
    //     level %d, sublevel %d (start type %d).", GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey, static_cast<int>(startType))
    // L2_LOG_ERROR(Log,
    //     L"[skills] Passive effect start rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked
    //     level %d, sublevel %d (start type %d).", GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey, static_cast<int>(startType))
    L2_LOG_ERROR(
        Log,
        u"[skills] Passive effect start rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked "
        u"level %d, sublevel %d (start type %d).",
        GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey, pumpInfo.first.m_SecondaryKey,
        static_cast<int>(startType)
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

bool CCreature::SetPassiveSkillPumpEnd(int skillId, int level, int subLevel, SkillPumpEndType endType) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);

    const auto found = m_PassivePumpInfo.find(skillId);
    if (found == m_PassivePumpInfo.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Try to pump end, but NO PUMP INFO. "
        //     L"(skill:%d, %d %d) (pump_type:%d)",
        //     GetName(),
        //     skillId,
        //     level,
        //     subLevel,
        //     endType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[skills] Passive effect end rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (end type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(endType))
        // L2_LOG_ERROR(Log,
        //     L"[skills] Passive effect end rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (end type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(endType))
        L2_LOG_ERROR(
            Log,
            u"[skills] Passive effect end rejected for %s: no tracking entry for skill %d, level %d, sublevel %d (end type "
            u"%d).",
            GetName(), skillId, level, subLevel, static_cast<int>(endType)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    auto& pumpInfo = found->second;
    if (pumpInfo.first.m_PrimaryKey == level && pumpInfo.first.m_SecondaryKey == subLevel) {
        if (pumpInfo.second) {
            pumpInfo.second = false;
            return true;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Try to duplicated pump end. "
        //     L"(skill:%d, %d, %d) (pump_type:%d)",
        //     GetName(),
        //     skillId,
        //     level,
        //     subLevel,
        //     endType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[skills] Passive effect end ignored for %s: skill %d, level %d, sublevel %d is already inactive (end type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(endType))
        // L2_LOG_ERROR(Log,
        //     L"[skills] Passive effect end ignored for %s: skill %d, level %d, sublevel %d is already inactive (end type
        //     %d).", GetName(), skillId, level, subLevel, static_cast<int>(endType))
        L2_LOG_ERROR(
            Log,
            u"[skills] Passive effect end ignored for %s: skill %d, level %d, sublevel %d is already inactive (end type "
            u"%d).",
            GetName(), skillId, level, subLevel, static_cast<int>(endType)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s] Try to pump end with different level. "
    //     L"(skill:%d, %d %d) (org_level:%d) (pump_type:%d)",
    //     GetName(),
    //     skillId,
    //     level,
    //     pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey,
    //     endType)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[skills] Passive effect end rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked level
    //     %d, sublevel %d (end type %d).", GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey, static_cast<int>(endType))
    // L2_LOG_ERROR(Log,
    //     L"[skills] Passive effect end rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked level
    //     %d, sublevel %d (end type %d).", GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey,
    //     pumpInfo.first.m_SecondaryKey, static_cast<int>(endType))
    L2_LOG_ERROR(
        Log,
        u"[skills] Passive effect end rejected for %s, skill %d: requested level %d, sublevel %d differ from tracked level "
        u"%d, sublevel %d (end type %d).",
        GetName(), skillId, level, subLevel, pumpInfo.first.m_PrimaryKey, pumpInfo.first.m_SecondaryKey,
        static_cast<int>(endType)
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

void CCreature::SetTacticalSignToMe(int creatureId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    m_PartyListSetttingTacticalSignToMe.insert(creatureId);
}

void CCreature::BroadcastTacticalSignInfo() {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (int partyId : m_PartyListSetttingTacticalSignToMe) {
        CParty* const party = g_PartySmartIdManager.GetObjectW(partyId);
        if (party != nullptr && party->GetMaster() != nullptr) {
            const int signId = party->GetMaster()->GetTacticalSignId(*this);
            party->OnSetTacticalSign(*this, signId);
        }
    }
}

void CCreature::ResetAllTacticalSignToMe() {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (int partyId : m_PartyListSetttingTacticalSignToMe) {
        CParty* const party = g_PartySmartIdManager.GetObjectW(partyId);
        if (party != nullptr) {
            party->ResetTacticalSign(*this);
        }
    }
    m_PartyListSetttingTacticalSignToMe.clear();
}

void CCreature::ResetAllTacticalSignToMeExcludeMyParty() {
    L2_GUARD_FUNCTION();
    CParty* const myParty = GetParty();
    if (myParty == nullptr) {
        ResetAllTacticalSignToMe();
        return;
    }

    L2_GUARD(*m_lock);
    for (int partyId : m_PartyListSetttingTacticalSignToMe) {
        CParty* const party = g_PartySmartIdManager.GetObjectW(partyId);
        if (party != nullptr && party != myParty) {
            party->ResetTacticalSign(*this);
        }
    }
    m_PartyListSetttingTacticalSignToMe.clear();
    m_PartyListSetttingTacticalSignToMe.insert(static_cast<int>(myParty->m_nId));
}

void CCreature::AccumulateContribute(CCreature* contributor, double contribution) {
    L2_GUARD_FUNCTION();
    m_pContribute->AccumulateContribute(contributor, contribution);

    const int pledgeId = static_cast<int>(contributor->m_data->m_nPledgeID);
    if (pledgeId == 0 || m_nPledgeContributeSaveTick == 0) {
        return;
    }

    L2_GUARD(m_PledgeContributeSect);
    const auto found = m_PledgeContributes.find(pledgeId);
    if (found == m_PledgeContributes.end()) {
        m_PledgeContributes.emplace(pledgeId, contribution);
    } else {
        found->second += contribution;
    }
}

void CCreature::SafeAccumulateContribute(CCreature* attacker, double contribution) {
    // Current L2Server 0x0060bb14.
    L2_GUARD_FUNCTION();
    if (attacker == nullptr || contribution <= 0.0 || GetUserOrMaster() != nullptr) {
        return;
    }

    CCreature* contributor = nullptr;
    if (attacker->IsSummonOrPet()) {
        contributor = attacker->GetUserOrMaster();
    } else if (attacker->IsNPC()) {
        CNPC* const npc = attacker->CastNPC();
        contributor = npc != nullptr ? npc->GetMaster() : nullptr;
    }

    if (contributor != nullptr && attacker->m_data->m_nMasterExpRatio >= 0) {
        AccumulateContribute(contributor, contribution);
        return;
    }
    AccumulateContribute(attacker, contribution);
}

bool CCreature::HasPassiveSkill(int skillId, int, int) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    return m_PassivePumpInfo.find(skillId) != m_PassivePumpInfo.end();
}

int CCreature::GetOnGroupToggleSkillId(int groupId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_GroupToggleSkills.find(groupId);
    return found == m_GroupToggleSkills.end() ? 0 : found->second;
}

bool CCreature::_GroupToggleSkillOn(int skillId, int groupId) {
    L2_GUARD_FUNCTION();
    const auto group = m_mapToggleGroup.find(groupId);
    if (group == m_mapToggleGroup.end()) {
        return false;
    }

    bool foundRequestedSkill = false;
    for (const int groupedSkillId : group->second) {
        const auto toggle = m_ToggleSkills.find(groupedSkillId);
        if (toggle == m_ToggleSkills.end()) {
            continue;
        }
        toggle->second.second = groupedSkillId == skillId;
        if (groupedSkillId == skillId) {
            foundRequestedSkill = true;
        }
    }

    const auto current = m_GroupToggleSkills.find(groupId);
    if (current == m_GroupToggleSkills.end()) {
        m_GroupToggleSkills.emplace(groupId, skillId);
    } else {
        current->second = skillId;
    }
    return foundRequestedSkill;
}

bool CCreature::GroupAuraSkillOnPumpEnd(int skillId, int groupId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);

    const auto group = m_mapToggleGroup.find(groupId);
    if (group == m_mapToggleGroup.end()) {
        return false;
    }

    bool foundRequestedSkill = false;
    for (const int groupedSkillId : group->second) {
        const auto aura = m_AuraSkills.find(groupedSkillId);
        if (aura == m_AuraSkills.end()) {
            continue;
        }
        if (groupedSkillId == skillId) {
            aura->second.second = true;
            foundRequestedSkill = true;
        } else if (aura->second.second) {
            aura->second.second = false;
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *aura->second.first, SPET_AURA));
        }
    }

    const auto current = m_GroupToggleSkills.find(groupId);
    if (current == m_GroupToggleSkills.end()) {
        m_GroupToggleSkills.emplace(groupId, skillId);
    } else {
        current->second = skillId;
    }
    return foundRequestedSkill;
}

bool CCreature::GroupToggleSkillOnPumpEnd(int skillId, int groupId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);

    const auto group = m_mapToggleGroup.find(groupId);
    if (group == m_mapToggleGroup.end()) {
        return false;
    }

    bool foundRequestedSkill = false;
    for (const int groupedSkillId : group->second) {
        const auto toggle = m_ToggleSkills.find(groupedSkillId);
        if (toggle == m_ToggleSkills.end()) {
            continue;
        }
        if (groupedSkillId == skillId) {
            toggle->second.second = true;
            foundRequestedSkill = true;
        } else if (toggle->second.second) {
            toggle->second.second = false;
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *toggle->second.first, SPET_TOGGLE));
        }
    }

    const auto current = m_GroupToggleSkills.find(groupId);
    if (current == m_GroupToggleSkills.end()) {
        m_GroupToggleSkills.emplace(groupId, skillId);
    } else {
        current->second = skillId;
    }
    return foundRequestedSkill;
}

void CCreature::OnGroupToggleState(int count, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    ::std::map<int, int> states;
    for (int index = 0; index < count; ++index) {
        int groupId = 0;
        int skillId = 0;
        packet = ::Disassemble(packet, "dd", &groupId, &skillId);
        states.emplace(groupId, skillId);
    }

    L2_GUARD(m_SkillListSect);
    for (const auto& toggle : m_ToggleSkills) {
        const CSkillInfo* const skill = toggle.second.first;
        if (skill->m_nToggleGroupId <= 0 || !skill->m_bIsNeccessaryTG) {
            continue;
        }

        const int groupId = skill->m_nToggleGroupId;
        const auto state = states.find(groupId);
        int activeSkillId = 0;
        if (state != states.end()) {
            activeSkillId = state->second;
        } else {
            const auto group = m_mapToggleGroup.find(groupId);
            if (group == m_mapToggleGroup.end()) {
                continue;
            }
            activeSkillId = group->second.front();
        }
        _GroupToggleSkillOn(activeSkillId, groupId);
    }
}

bool CCreature::ToggleSkillOnOff(int skillId, bool enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_ToggleSkills.find(skillId);
    if (found == m_ToggleSkills.end()) {
        return false;
    }

    found->second.second = enabled;
    if (IsPC()) {
        ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SKILL});
    }
    return true;
}

bool CCreature::IsToggleSkillOnOff(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SkillListSect);
    const auto found = m_ToggleSkills.find(skillId);
    return found != m_ToggleSkills.end() && found->second.second;
}

int CCreature::AuraSkillAllOff(bool notifyClient) {
    // Current L2Server 0x0061c1b8.
    L2_GUARD_FUNCTION();

    int disabled = 0;
    {
        L2_GUARD(m_SkillListSect);
        for (auto& entry : m_AuraSkills) {
            const CSkillInfo* const skillInfo = entry.second.first;
            if (!entry.second.second) {
                continue;
            }
            entry.second.second = false;
            ++disabled;
            if (skillInfo->m_nToggleGroupId > 0) {
                m_GroupToggleSkills.erase(skillInfo->m_nToggleGroupId);
            }
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, SPET_AURA));
        }
    }

    if (notifyClient && disabled > 0) {
        if (IsPC()) {
            CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
        if (IsPC()) {
            m_bAbnormalStatusChanged = true;
        }
        CheckAbnormalVisualEffect();
    }

    {
        L2_GUARD(m_SkillMod.m_Lock);
        m_SkillMod.m_SkillAuraMap.clear();
        m_SkillMod.m_SkillSynergyAuraMap.clear();
    }
    return disabled;
}

int CCreature::ToggleSkillAllOff(bool notifyClient) {
    // Current L2Server 0x0061c414.
    L2_GUARD_FUNCTION();

    int disabled = 0;
    {
        L2_GUARD(m_SkillListSect);
        for (auto& entry : m_ToggleSkills) {
            const CSkillInfo* const skillInfo = entry.second.first;
            if (!entry.second.second) {
                continue;
            }
            entry.second.second = false;
            ++disabled;
            if (skillInfo->m_nToggleGroupId > 0) {
                m_GroupToggleSkills.erase(skillInfo->m_nToggleGroupId);
            }
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, SPET_TOGGLE));
        }
    }

    if (m_data->m_bIsFakeDeath) {
        m_data->m_StopMode = STP_STAND;
        GetController()->Stand(0);
        m_data->m_bIsFakeDeath = false;
    }

    if (notifyClient && disabled > 0) {
        if (IsPC()) {
            CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
        if (IsPC()) {
            m_bAbnormalStatusChanged = true;
        }
        CheckAbnormalVisualEffect();
    }
    return disabled;
}

bool CCreature::_HaveSkill(int skillId, int level) const {
    L2_GUARD_FUNCTION();
    const auto found = m_AcquiredSkills.find(skillId);
    return found != m_AcquiredSkills.end() && found->second.m_nLevel == level;
}

bool CCreature::_HaveSkill(int skillId, int level, int subLevel) const {
    L2_GUARD_FUNCTION();
    const auto found = m_AcquiredSkills.find(skillId);
    return found != m_AcquiredSkills.end() && found->second.m_nLevel == level && found->second.m_nSubLevel == subLevel;
}

int CCreature::IsLockedSkill(int skillId) {
    L2_GUARD_FUNCTION();
    decltype(m_SkillLockMap)::iterator found;
    {
        L2_GUARD(m_SkillLockSect);
        found = m_SkillLockMap.find(skillId);
        if (found == m_SkillLockMap.end()) {
            return 0;
        }
    }
    return found->second;
}

void CCreature::SetSkillLock(const int skillId, const int skillLevel, const int skillSubLevel, const int lockValue) {
    L2_GUARD_FUNCTION();
    bool changed = false;
    {
        L2_GUARD(m_SkillLockSect);
        const auto found = m_SkillLockMap.find(skillId);
        if (found == m_SkillLockMap.end()) {
            changed = lockValue != 0;
            m_SkillLockMap.emplace(skillId, lockValue);
        } else {
            changed = found->second != lockValue;
            found->second = lockValue;
        }
    }

    if (!changed) {
        return;
    }
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), this);
    if (skill == nullptr || (skill->m_OperateType != SKILL_PASSIVE && skill->m_OperateType != SKILL_RACE_SPECIAL_PASSIVE)) {
        return;
    }

    CSerializedMessage* message = nullptr;
    if (lockValue == 1) {
        message = new CSerializedPumpEndMessage(this, this, *skill, SPET_SKILL_LOCK);
    } else {
        message = new CSerializedOnInfectedSkillByMessage(this, this, *skill, SPST_SKILL_UNLOCK, false);
    }
    PushSerializedMessage(message);
}

int CCreature::GetAcquiredSkillLevel(int skillId) {
    L2_GUARD_FUNCTION();
    int result;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        const auto found = m_AcquiredSkills.find(skillId);
        result = found == m_AcquiredSkills.end() ? -1 : found->second.m_nLevel;
    }
    return result;
}

int CCreature::GetAcquiredSkillSubLevel(int skillId) {
    L2_GUARD_FUNCTION();
    int result;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        const auto found = m_AcquiredSkills.find(skillId);
        result = found == m_AcquiredSkills.end() ? 0 : found->second.m_nSubLevel;
    }
    return result;
}

void CCreature::_OnDeletedSkill(AcquiredSkillInfo& skillInfo) {
    L2_GUARD_FUNCTION();
    const CSkillKey skillKey(skillInfo.m_nId, skillInfo.m_nLevel, skillInfo.m_nSubLevel);
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(skillKey, this);
    if (skill != nullptr && (skill->m_OperateType == SKILL_PASSIVE || skill->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE)) {
        PushSerializedMessage(new CSerializedPumpEndMessage(nullptr, this, *skill, SPET_DELETE_SKILL));
    }
}

bool CCreature::_AcquireSkill(int skillId, int level, int subLevel, bool replace, bool saveToDatabase) {
    L2_GUARD_FUNCTION();
    const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), this);
    if (skill == nullptr) {
        return false;
    }
    if (skill->m_bInstantItem) {
        if (IsPC()) {
            CastUser()->LogAbnormalAction(AUA_ACQUIRE_INSTANT_SKILL, false, 0, 0, 0, 0);
        }
        return false;
    }

    if (g_SkillAcquireDB.IsMatchSkillAcquireType(ESAT_HONORS, skillId, level)) {
        if (!IsPC()) {
            return false;
        }
        User* const user = CastUser();
        if ((user->m_data->m_nNoblessType != 1 && user->m_data->m_nNoblessType != 2) ||
            (CastUser()->m_data->m_nSubJobId != 0 && !CastUser()->m_data->m_IsDualClass) ||
            !CastUser()->SetHonorsSkillList(skillId, level, false)) {
            return false;
        }
    }

    const auto inserted = m_AcquiredSkills.emplace(
        skillId, AcquiredSkillInfo(skillId, level, subLevel, skill->m_nReuseDelayGroup, saveToDatabase)
    );
    if (!inserted.second) {
        AcquiredSkillInfo& current = inserted.first->second;
        const bool isUpgrade = (current.m_nLevel < level && current.m_nSubLevel == subLevel) ||
            (current.m_nLevel == level && current.m_nSubLevel < subLevel);
        if (!replace && !isUpgrade) {
            return false;
        }

        const CSkillInfo* const oldSkill =
            g_SkillDB.GetSkillInfo(CSkillKey(current.m_nId, current.m_nLevel, current.m_nSubLevel), this);
        if (oldSkill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Skill ID[%d] level[%d] doesn't exist",
            //     kCreatureServerSourceFile,
            //     0x3908,
            //     current.m_nId,
            //     current.m_nLevel)
            // Log.Add(
            //     LOG_ERROR,
            //     "[skills] Cannot replace acquired skill for object %u: stored skill %d, level %d, sublevel %d has no
            //     definition. Requested skill %d, level %d, sublevel %d.", m_nId, current.m_nId, current.m_nLevel,
            //     current.m_nSubLevel, skillId, level, subLevel)
            L2_LOG_ERROR(
                Log,
                "[skills] Cannot replace acquired skill for object %u: stored skill %d, level %d, sublevel %d has no "
                "definition; requested skill %d, level %d, sublevel %d.",
                m_nId, current.m_nId, current.m_nLevel, current.m_nSubLevel, skillId, level, subLevel
            );
            return false;
        }
        if (oldSkill->m_OperateType == SKILL_PASSIVE || oldSkill->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE) {
            PushSerializedMessage(new CSerializedPumpEndMessage(nullptr, this, *oldSkill, SPET_DELETE_SKILL));
        }
        if (isUpgrade) {
            m_SkillReuseDelay.CheckAndRemoveSkillUsableTime(skillId, level, subLevel);
        }
        current.m_nLevel = level;
        current.m_nSubLevel = subLevel;
        current.m_bSaveToDB = saveToDatabase;
    }

    if (skill->m_OperateType == SKILL_PASSIVE || skill->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE) {
        PushSerializedMessage(new CSerializedOnInfectedSkillByMessage(this, this, *skill, SPST_ACQUIRE_SKILL, false));
    }
    return true;
}

void CCreature::_GetPassiveSkillListByType(const CSkillInfo* skillInfo, std::vector<const CSkillInfo*>& passiveSkills) {
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr ||
        (skillInfo->m_OperateType != SKILL_PASSIVE && skillInfo->m_OperateType != SKILL_RACE_SPECIAL_PASSIVE)) {
        return;
    }

    if (skillInfo->m_nSkillId == 14571 || skillInfo->m_nSkillId == 23344) {
        m_data->m_nDeathPenaltyLevel = skillInfo->m_nLevel;
    }
    passiveSkills.push_back(skillInfo);
}

void CCreature::_GetSkillListByType(
    const CSkillInfo* skillInfo,
    std::vector<const CSkillInfo*>& activeSkills,
    std::vector<const CSkillInfo*>& passiveSkills,
    std::vector<const CSkillInfo*>& toggleSkills,
    std::vector<const CSkillInfo*>& auraSkills
) {
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return;
    }

    if (!skillInfo->IsActiveSkill()) {
        _GetPassiveSkillListByType(skillInfo, passiveSkills);
        return;
    }

    if (skillInfo->m_OperateType == SKILL_TOGGLE || skillInfo->m_OperateType == SKILL_GROUP_TOGGLE) {
        toggleSkills.push_back(skillInfo);
    } else if (skillInfo->m_OperateType == SKILL_AURA) {
        auraSkills.push_back(skillInfo);
    } else {
        activeSkills.push_back(skillInfo);
    }
}

void CCreature::GetSkillListByType(
    std::vector<const CSkillInfo*>& activeSkills,
    std::vector<const CSkillInfo*>& passiveSkills,
    std::vector<const CSkillInfo*>& toggleSkills,
    std::vector<const CSkillInfo*>& auraSkills
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    m_data->m_nDeathPenaltyLevel = 0;
    for (const auto& acquiredSkill : m_AcquiredSkills) {
        const CSkillKey skillKey(
            acquiredSkill.second.m_nId, acquiredSkill.second.m_nLevel, acquiredSkill.second.m_nSubLevel
        );
        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(skillKey, this);
        _GetSkillListByType(skillInfo, activeSkills, passiveSkills, toggleSkills, auraSkills);
    }
}

void CCreature::ResetSkillsAfterValidateSkill(
    std::vector<const CSkillInfo*>& activeSkills,
    std::vector<const CSkillInfo*>& passiveSkills,
    std::vector<const CSkillInfo*>& toggleSkills,
    std::vector<const CSkillInfo*>& auraSkills
) {
    L2_GUARD_FUNCTION();

    std::optional<::std::set<int>> enabledAuraSkillIdsStorage;
    std::optional<::std::set<int>> enabledToggleSkillIdsStorage;
    {
        L2_GUARD(m_SkillListSect);

        m_ActiveSkills.clear();
        for (const CSkillInfo* const skillInfo : activeSkills) {
            m_ActiveSkills.emplace(skillInfo->m_nSkillId, skillInfo);
        }

        auto& enabledAuraSkillIds = enabledAuraSkillIdsStorage.emplace();
        for (const auto& auraSkill : m_AuraSkills) {
            if (auraSkill.second.second) {
                enabledAuraSkillIds.insert(auraSkill.second.first->m_nSkillId);
            }
        }
        m_AuraSkills.clear();
        for (const CSkillInfo* const skillInfo : auraSkills) {
            m_AuraSkills.emplace(
                skillInfo->m_nSkillId,
                std::make_pair(skillInfo, enabledAuraSkillIds.find(skillInfo->m_nSkillId) != enabledAuraSkillIds.end())
            );
        }

        m_PassiveSkills.clear();
        for (const CSkillInfo* const skillInfo : passiveSkills) {
            m_PassiveSkills.push_back(skillInfo);
        }

        auto& enabledToggleSkillIds = enabledToggleSkillIdsStorage.emplace();
        for (const auto& toggleSkill : m_ToggleSkills) {
            if (toggleSkill.second.second) {
                enabledToggleSkillIds.insert(toggleSkill.second.first->m_nSkillId);
            }
        }
        m_ToggleSkills.clear();
        for (const CSkillInfo* const skillInfo : toggleSkills) {
            m_ToggleSkills.emplace(
                skillInfo->m_nSkillId,
                std::make_pair(skillInfo, enabledToggleSkillIds.find(skillInfo->m_nSkillId) != enabledToggleSkillIds.end())
            );
        }
    }
}

void CCreature::ResetSkillsAfterValidateSkillForTransform(
    std::vector<const CSkillInfo*>& activeSkills,
    std::vector<const CSkillInfo*>& passiveSkills,
    std::vector<const CSkillInfo*>& toggleSkills,
    std::vector<const CSkillInfo*>& auraSkills
) {
    L2_GUARD_FUNCTION();

    std::optional<::std::set<int>> enabledAuraSkillIdsStorage;
    std::optional<::std::set<int>> enabledToggleSkillIdsStorage;
    {
        L2_GUARD(m_SkillListSect);

        m_ActiveSkills.clear();
        for (const CSkillInfo* const skillInfo : activeSkills) {
            m_ActiveSkills.emplace(skillInfo->m_nSkillId, skillInfo);
        }

        auto& enabledAuraSkillIds = enabledAuraSkillIdsStorage.emplace();
        for (auto& auraSkill : m_AuraSkills) {
            const CSkillInfo* const skillInfo = auraSkill.second.first;
            if (auraSkill.second.second && skillInfo != nullptr) {
                enabledAuraSkillIds.insert(skillInfo->m_nSkillId);
                PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, SPET_TOGGLE));
                auraSkill.second.second = false;
                if (skillInfo->m_nToggleGroupId > 0) {
                    m_GroupToggleSkills.erase(skillInfo->m_nToggleGroupId);
                }
            }
        }
        m_AuraSkills.clear();
        for (const CSkillInfo* const skillInfo : auraSkills) {
            m_AuraSkills.emplace(
                skillInfo->m_nSkillId,
                std::make_pair(skillInfo, enabledAuraSkillIds.find(skillInfo->m_nSkillId) != enabledAuraSkillIds.end())
            );
        }

        m_PassiveSkills.clear();
        for (const CSkillInfo* const skillInfo : passiveSkills) {
            m_PassiveSkills.push_back(skillInfo);
        }

        auto& enabledToggleSkillIds = enabledToggleSkillIdsStorage.emplace();
        for (auto& toggleSkill : m_ToggleSkills) {
            const CSkillInfo* const skillInfo = toggleSkill.second.first;
            if (toggleSkill.second.second && skillInfo != nullptr) {
                enabledToggleSkillIds.insert(skillInfo->m_nSkillId);
                PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, SPET_TOGGLE));
                toggleSkill.second.second = false;
                if (skillInfo->m_nToggleGroupId > 0) {
                    m_GroupToggleSkills.erase(skillInfo->m_nToggleGroupId);
                }
                if (IsPC() && m_data->m_bIsFakeDeath) {
                    GetController()->Stand(0);
                }
            }
        }
        m_ToggleSkills.clear();
        for (const CSkillInfo* const skillInfo : toggleSkills) {
            m_ToggleSkills.emplace(
                skillInfo->m_nSkillId,
                std::make_pair(skillInfo, enabledToggleSkillIds.find(skillInfo->m_nSkillId) != enabledToggleSkillIds.end())
            );
        }
    }
    m_bAbnormalStatusChanged = true;
}

void CCreature::ValidateSkillList() {
    L2_GUARD_FUNCTION();
    std::vector<const CSkillInfo*> activeSkills;
    std::vector<const CSkillInfo*> passiveSkills;
    std::vector<const CSkillInfo*> toggleSkills;
    std::vector<const CSkillInfo*> auraSkills;
    GetSkillListByType(activeSkills, passiveSkills, toggleSkills, auraSkills);
    if (std::getenv("L2_MAGIC_TRACE") != nullptr && IsPC()) {
        // std::fprintf(
        //     stderr,
        //     "[MAGIC_TRACE] validate-skill-list sid=%u dbid=%d "
        //     "class=%d level=%d active=%zu passive=%zu toggle=%zu aura=%zu\n",
        //     Id(),
        //     static_cast<int>(GetDBID()),
        //     static_cast<int>(m_data->m_nClass),
        //     m_data->m_nLevel,
        //     activeSkills.size(),
        //     passiveSkills.size(),
        //     toggleSkills.size(),
        //     auraSkills.size())
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Info,
        //     "[magic trace] Rebuilding skill lists for object %u (DB ID %u), class %d, level %d: active %zu, passive %zu,
        //     toggle %zu, aura %zu.", Id(), GetDBID(), static_cast<int>(m_data->m_nClass), m_data->m_nLevel,
        //     activeSkills.size(),
        //     passiveSkills.size(),
        //     toggleSkills.size(),
        //     auraSkills.size())
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Info,
            "[magic trace] Rebuilding skill lists for object %u (DB ID %u), class %d, level %d: active %zu, passive %zu, "
            "toggle %zu, aura %zu.",
            Id(), GetDBID(), static_cast<int>(m_data->m_nClass), m_data->m_nLevel, activeSkills.size(), passiveSkills.size(),
            toggleSkills.size(), auraSkills.size()
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
    }
    ResetSkillsAfterValidateSkill(activeSkills, passiveSkills, toggleSkills, auraSkills);
}

void CCreature::SetMPReward(CCreature* victim, CCreature* contributor) {
    // Current L2Server 0x0060755c.
    L2_GUARD_FUNCTION();
    if (victim == nullptr || contributor == nullptr || !IsPCOrSummon() || !victim->IsNPC()) {
        return;
    }

    CNPC* const npc = victim->CastNPC();
    if (npc == nullptr || npc->m_pClassConst == nullptr) {
        return;
    }

    MPRewardData reward = npc->m_pClassConst->m_MPRewardData;
    if (reward.Type() == MPRewardData::TypeNone || reward.Scope() == MPRewardData::ScopeNone) {
        return;
    }

    if (reward.Scope() == MPRewardData::Solo) {
        if (GetSmartID().Sid() != contributor->GetSmartID().Sid()) {
            return;
        }
    } else if (reward.Scope() == MPRewardData::Party) {
        CParty* const contributorParty = contributor->GetParty();
        if (contributorParty == nullptr) {
            if (GetSmartID().Sid() != contributor->GetSmartID().Sid()) {
                return;
            }
        } else {
            CParty* const party = GetParty();
            if (party == nullptr || party->GetSmartID().Sid() != contributorParty->GetSmartID().Sid()) {
                return;
            }
        }
    }

    if (CParty* const party = GetParty()) {
        const int memberCount = party->GetMemberCount();
        if (memberCount > 10) {
            return;
        }
        if (memberCount >= 6) {
            reward.Amount(std::max(0.0, reward.Amount() * 0.5));
        }
    }

    L2_GUARD(*m_lock);
    m_MPRewardList.push_back(reward);
}

void CCreature::ApplyMPReward() {
    L2_GUARD_FUNCTION();
    if (!IsPCOrSummon()) {
        return;
    }

    L2_GUARD(*m_lock);
    if (m_MPRewardList.empty()) {
        return;
    }

    double percentage = 0.0;
    double difference = 0.0;
    auto reward = m_MPRewardList.begin();
    while (reward != m_MPRewardList.end()) {
        if (!reward->Expired()) {
            reward->TickInc();
            if (reward->Type() == MPRewardData::Per) {
                percentage += reward->Amount();
            } else if (reward->Type() == MPRewardData::Diff) {
                difference += reward->Amount();
            }
            ++reward;
        } else {
            reward = m_MPRewardList.erase(reward);
        }
    }

    IncreaseMP_diff(percentage / 100.0 * m_data->m_dBaseMP + difference, 0, 0, false);
}

bool CCreature::DespawnCubicBySlot(int slot) {
    L2_GUARD_FUNCTION();
    bool despawned = false;
    {
        L2_GUARD(m_spriteLock);
        const auto cubic = m_cubicMap.find(slot);
        if (cubic != m_cubicMap.end()) {
            m_cubicMap.erase(cubic);
            despawned = true;
        }
    }

    if (despawned) {
        m_data->SetNeedUpdatePacket(74);
        if (IsPC()) {
            ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CUBIC});
        } else {
            ParamChanged();
        }
    }
    return despawned;
}

void CCreature::DespawnAllCubic(bool notifyParameterChange) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_spriteLock);
    if (!m_cubicMap.empty()) {
        m_cubicMap.clear();
        m_data->SetNeedUpdatePacket(74);
        if (notifyParameterChange) {
            if (IsPC()) {
                ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CUBIC});
            } else {
                ParamChanged();
            }
        }
    }
}

void CCreature::EraseCubicNotSelfCast() {
    L2_GUARD_FUNCTION();
    bool erased = false;
    {
        L2_GUARD(m_spriteLock);
        auto cubic = m_cubicMap.begin();
        while (cubic != m_cubicMap.end()) {
            if (!cubic->second.second.bySelfCasting) {
                cubic = m_cubicMap.erase(cubic);
                erased = true;
            } else {
                ++cubic;
            }
        }
    }

    if (erased) {
        m_data->SetNeedUpdatePacket(74);
        if (IsPC()) {
            ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CUBIC});
        } else {
            ParamChanged();
        }
    }
}

bool CCreature::SummonCubic(int cubicId, int cubicLevel, bool bySelfCasting) {
    // Current L2Server 0x0061a358.
    L2_GUARD_FUNCTION();
    if (m_SkillDurationMod.m_AntiCubic >= 1) {
        return false;
    }

    CSprite* const sprite = SpriteDB::Inst().GetInfo(SPRITE_CUBIC, cubicId, cubicLevel);
    CCubic* const cubic = sprite == nullptr ? nullptr : sprite->CastCubic();
    if (cubic == nullptr) {
        return false;
    }

    std::int32_t summonedTime = 0;
    CIOObject::SetTimeStamp(&summonedTime);
    {
        L2_GUARD(m_spriteLock);

        for (auto position = m_cubicMap.begin(); position != m_cubicMap.end(); ++position) {
            CCubic* const previous = position->second.first;
            if (previous == nullptr || previous->m_slot != cubic->m_slot) {
                continue;
            }
            if (previous->m_classID == cubic->m_classID && cubic->m_level < previous->m_level) {
                return false;
            }
            m_cubicMap.erase(position);
            break;
        }

        if (static_cast<std::size_t>(m_maxCubic) <= m_cubicMap.size()) {
            m_cubicMap.erase(m_cubicMap.begin());
        }

        m_cubicMap.emplace(cubic->m_slot, std::make_pair(cubic, CubicActivationInfo{summonedTime, bySelfCasting}));
    }

    m_data->SetNeedUpdatePacket(74);
    if (IsPC()) {
        ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CUBIC});
    } else {
        ParamChanged();
    }
    return true;
}

bool CCreature::_DeleteAcquireSkill(int skillId) {
    L2_GUARD_FUNCTION();
    const auto found = m_AcquiredSkills.find(skillId);
    if (found == m_AcquiredSkills.end()) {
        return false;
    }
    _OnDeletedSkill(found->second);
    m_AcquiredSkills.erase(found);
    return true;
}

void CCreature::_DeleteAllAcquireSkills(bool preserveDatabaseSkills) {
    // Current L2Server 0x00613bfc.
    L2_GUARD_FUNCTION();
    for (auto position = m_AcquiredSkills.begin(); position != m_AcquiredSkills.end();) {
        if (!preserveDatabaseSkills) {
            _OnDeletedSkill(position->second);
            ++position;
            continue;
        }

        const AcquiredSkillInfo& acquired = position->second;
        const CSkillInfo* const skill =
            g_SkillDB.GetSkillInfo(CSkillKey(acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel), this);
        if (skill->m_bSaveToDB) {
            ++position;
            continue;
        }

        _OnDeletedSkill(position->second);
        position = m_AcquiredSkills.erase(position);
    }
    if (!preserveDatabaseSkills) {
        m_AcquiredSkills.clear();
    }
}

void CCreature::DeleteAllAcquireSkills(const bool preserveDatabaseSkills) {
    // Current L2Server 0x0061a16c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AcquiredSkillsSect);
        _DeleteAllAcquireSkills(preserveDatabaseSkills);
    }
    AcquireSkills(g_SkillAcquireDB.GetAcquirableRaceSkills(m_data->m_Race), true, false);
}

bool CCreature::AcquireSkill(int skillId, int level, int subLevel, bool replace, bool saveToDatabase) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    return _AcquireSkill(skillId, level, subLevel, replace, saveToDatabase);
}

void CCreature::ModifyAcquireSkill(int skillId, int level, int subLevel, bool replace) {
    // Current L2Server 0x00613ea4.
    L2_GUARD_FUNCTION();
    AcquireSkill(skillId, level, subLevel, replace, true);
}

void CCreature::_AcquireSkills(const std::vector<SkillAcquireInfo*>& skills, bool autoOnly, bool replace) {
    // Current L2Server 0x0060ecc0.
    L2_GUARD_FUNCTION();

    const int creatureLevel = m_data->m_nLevel;
    const bool magicTrace = std::getenv("L2_MAGIC_TRACE") != nullptr && IsPC();
    std::size_t skippedLevel = 0;
    std::size_t skippedAuto = 0;
    std::size_t skippedRace = 0;
    std::size_t attempted = 0;
    std::size_t acquired = 0;
    for (SkillAcquireInfo* const skill : skills) {
        if (skill->nGetLevel > creatureLevel) {
            ++skippedLevel;
            continue;
        }
        if (autoOnly && !skill->bAutoGet) {
            ++skippedAuto;
            continue;
        }

        if (skill->bAutoGet && !skill->m_RaceConditions.empty() &&
            skill->m_RaceConditions.find(m_data->m_Race) == skill->m_RaceConditions.end()) {
            ++skippedRace;
            continue;
        }

        const bool saveToDatabase = !skill->bAutoGet && !skill->m_bMentorMenteeSkill;
        ++attempted;
        if (_AcquireSkill(skill->nSkillID, skill->nSkillLevel, 0, replace, saveToDatabase)) {
            ++acquired;
        }
    }
    if (magicTrace) {
        // std::fprintf(
        //     stderr,
        //     "[MAGIC_TRACE] acquire-skills-summary sid=%u dbid=%d "
        //     "class=%d level=%d candidates=%zu auto-only=%d replace=%d "
        //     "attempted=%zu acquired=%zu skipped-level=%zu "
        //     "skipped-auto=%zu skipped-race=%zu\n",
        //     Id(),
        //     static_cast<int>(GetDBID()),
        //     static_cast<int>(m_data->m_nClass),
        //     creatureLevel,
        //     skills.size(),
        //     autoOnly ? 1 : 0,
        //     replace ? 1 : 0,
        //     attempted,
        //     acquired,
        //     skippedLevel,
        //     skippedAuto,
        //     skippedRace)
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Info,
        //     "[magic trace] Skill acquisition pass for object %u (DB ID %u), class %d, level %d: candidates %zu, automatic
        //     only: %s, replace: %s. Attempted %zu, acquisition calls returning true %zu. Skipped by level %zu,
        //     automatic-only filter %zu, race %zu.", Id(), GetDBID(), static_cast<int>(m_data->m_nClass), creatureLevel,
        //     skills.size(),
        //     autoOnly ? "yes" : "no",
        //     replace ? "yes" : "no",
        //     attempted,
        //     acquired,
        //     skippedLevel,
        //     skippedAuto,
        //     skippedRace)
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Info,
            "[magic trace] Skill acquisition pass for object %u (DB ID %u), class %d, level %d: candidates %zu, automatic "
            "only: %s, replace: %s; attempted %zu, acquisition calls returning true %zu; skipped by level %zu, "
            "automatic-only filter %zu, race %zu.",
            Id(), GetDBID(), static_cast<int>(m_data->m_nClass), creatureLevel, skills.size(), autoOnly ? "yes" : "no",
            replace ? "yes" : "no", attempted, acquired, skippedLevel, skippedAuto, skippedRace
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
    }
}

void CCreature::AcquireSkills(const std::vector<SkillAcquireInfo*>& skills, bool autoOnly, bool replace) {
    // Current L2Server 0x00613b08.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    _AcquireSkills(skills, autoOnly, replace);
}

void CCreature::OnInfectedSkillBy(const CSkillInfo& skillInfo, bool needSendVisualEffect) {
    L2_GUARD_FUNCTION();
    if (!IsPC()) {
        SetSpelledInfoChanged();
    } else {
        User* const user = CastUser();
        user->m_bSpelledInfoChanged = true;
        user->m_NeedUpdatePacketBits |= 2U;
    }

    if (!IsPC()) {
        ValidateChanged(VB_BASEDATA | VB_BASEATTR | VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    } else {
        CastUser()->ValidateChanged(
            VB_BASEDATA | VB_BASEATTR | VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED
        );
    }

    {
        L2_GUARD(m_SkillListSect);
    }

    if (needSendVisualEffect || !skillInfo.m_AbnormalVisualEffect.empty()) {
        CheckAbnormalVisualEffect();
    }
}

int CCreature::ChangeScaleMode(int scaleMode) {
    L2_GUARD_FUNCTION();
    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
            m_data->m_dCollisionRadius, npc->m_pClassConst->m_dCollisionRadius[scaleMode], 0x1b
        );
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
            m_data->m_dCollisionHeight, npc->m_pClassConst->m_dCollisionHeight[scaleMode], 0x1c
        );
    }
    return 0;
}

bool CCreature::_CheckAbnormalVisualEffect_InfectedSkill(::std::set<int>& visualEffects) {
    L2_GUARD_FUNCTION();

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    bool forceUpdate = false;
    std::int32_t maximumRemainingTime = 0;
    const int skillCount = static_cast<int>(m_InfectedSkills.size());
    for (int index = 0; index < skillCount; ++index) {
        const InfectedSkillType& infectedSkill = runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index));
        const CSkillInfo* const skillInfo = infectedSkill.pSkillInfo;
        if (skillInfo->m_AbnormalVisualEffect.empty()) {
            continue;
        }

        for (const int visualEffect : skillInfo->m_AbnormalVisualEffect) {
            visualEffects.insert(visualEffect);
        }

        // The current image performs this first call for every
        // non--2 abnormal slot and then performs the common call below.
        if (static_cast<int>(skillInfo->m_nAbnormalType) != -2) {
            GetAbnormalTime(skillInfo, false);
        }
        const std::int32_t remainingTime = runtime::AddInt32Wrapping(
            GetAbnormalTime(skillInfo, false), runtime::SubtractInt32Wrapping(infectedSkill.nStartTime, now)
        );
        if (maximumRemainingTime < remainingTime) {
            forceUpdate = true;
            maximumRemainingTime = remainingTime;
        }
    }
    return forceUpdate;
}

void CCreature::_CheckAbnormalVisualEffect_ToggleSkill(::std::set<int>& visualEffects) {
    L2_GUARD_FUNCTION();

    for (const auto& toggleSkill : m_ToggleSkills) {
        const CSkillInfo* const skillInfo = toggleSkill.second.first;
        if (IsLockedSkill(toggleSkill.first) != 0 || !toggleSkill.second.second ||
            skillInfo->m_AbnormalVisualEffect.empty()) {
            continue;
        }
        for (const int visualEffect : skillInfo->m_AbnormalVisualEffect) {
            visualEffects.insert(visualEffect);
        }
    }
}

void CCreature::_CheckAbnormalVisualEffect_AuraSkill(::std::set<int>& visualEffects) {
    L2_GUARD_FUNCTION();

    for (const auto& auraSkill : m_AuraSkills) {
        const CSkillInfo* const skillInfo = auraSkill.second.first;
        if (IsLockedSkill(auraSkill.first) != 0 || !auraSkill.second.second || skillInfo->m_AbnormalVisualEffect.empty()) {
            continue;
        }
        for (const int visualEffect : skillInfo->m_AbnormalVisualEffect) {
            visualEffects.insert(visualEffect);
        }
    }
}

void CCreature::_ProcAbnormalVisualEffect(::std::set<int>& visualEffects, bool forceUpdate) {
    L2_GUARD_FUNCTION();

    if (m_data->m_nBuilder == 1) {
        for (const int visualEffect : m_AbnormalVisualEffectForTest) {
            visualEffects.insert(visualEffect);
        }
    }

    bool changed = m_AbnormalVisualEffectMap.size() != visualEffects.size();
    if (!changed) {
        auto current = m_AbnormalVisualEffectMap.begin();
        auto next = visualEffects.begin();
        while (current != m_AbnormalVisualEffectMap.end()) {
            if (*current != *next) {
                changed = true;
                break;
            }
            ++current;
            ++next;
        }
    }

    if (!changed && !forceUpdate) {
        return;
    }

    m_AbnormalVisualEffectMap.clear();
    for (const int visualEffect : visualEffects) {
        m_AbnormalVisualEffectMap.insert(visualEffect);
    }

    ChangeScaleMode(m_AbnormalVisualEffectMap.find(0x11) == m_AbnormalVisualEffectMap.end());
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x4b);
    if (IsPC()) {
        ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_ABNORMALVISUALEFFECT});
    } else {
        ParamChanged();
    }
}

void CCreature::CheckAbnormalVisualEffect() {
    L2_GUARD_FUNCTION();

    ::std::set<int> visualEffects;
    bool forceUpdate = false;
    {
        L2_GUARD(m_InfectedSkillsSect);
        forceUpdate = _CheckAbnormalVisualEffect_InfectedSkill(visualEffects);
    }
    {
        L2_GUARD(m_SkillListSect);
        _CheckAbnormalVisualEffect_ToggleSkill(visualEffects);
        _CheckAbnormalVisualEffect_AuraSkill(visualEffects);
    }
    {
        L2_GUARD(m_InfectedSkillsSect);
        _ProcAbnormalVisualEffect(visualEffects, forceUpdate);
    }
}

void CCreature::ActivateRecoverSkill() {
    // Current L2Server.exe 0x005f3f3c.
    L2_GUARD_FUNCTION();

    CSkillInfo* recoverSkill = nullptr;
    {
        L2_GUARD(m_SkillDurationMod.m_Lock);
        recoverSkill = m_SkillDurationMod.p_RecoverSkillInfo;
        if (recoverSkill != nullptr) {
            m_SkillDurationMod.p_RecoverSkillInfo = nullptr;
        }
    }

    if (recoverSkill != nullptr) {
        recoverSkill->ActivateSkill(this, this, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
}

void CCreature::_CheckAbnormalVisualEffect() {
    L2_GUARD_FUNCTION();

    ::std::set<int> visualEffects;
    const bool forceUpdate = _CheckAbnormalVisualEffect_InfectedSkill(visualEffects);
    {
        L2_GUARD(m_SkillListSect);
        _CheckAbnormalVisualEffect_ToggleSkill(visualEffects);
        _CheckAbnormalVisualEffect_AuraSkill(visualEffects);
    }
    _ProcAbnormalVisualEffect(visualEffects, forceUpdate);
}

bool CCreature::DeleteAcquireSkill(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    return _DeleteAcquireSkill(skillId);
}

double CCreature::GetUserPremiumExpBonus() {
    L2_GUARD_FUNCTION();
    if (IsPC()) {
        return F2PConfig::GetExpBoostRate(CastUser());
    }
    if (IsSummonOrPet()) {
        User* const user = GetUserOrMaster();
        if (user != nullptr) {
            return F2PConfig::GetExpBoostRate(user);
        }
    }
    return 0.0;
}

double CCreature::GetUserPremiumSpBonus() {
    L2_GUARD_FUNCTION();
    if (IsPC()) {
        return F2PConfig::GetSpBoostRate(CastUser());
    }
    if (IsSummonOrPet()) {
        User* const user = GetUserOrMaster();
        if (user != nullptr) {
            return F2PConfig::GetSpBoostRate(user);
        }
    }
    return 0.0;
}

double CCreature::GetPCCafeUserExpSPBonus() {
    // Current L2Server 0x004c6cc4.
    L2_GUARD_FUNCTION();
    CEvent* const baseEvent = CEventManager::inst().GetEvent(GAME_EVENT::ET_PCCAFE_SETTING);
    if (baseEvent == nullptr) {
        return 0.0;
    }
    return static_cast<double>(static_cast<CEvent_PCCafe*>(baseEvent)->GetPCCafeExpBonus()) / 100.0;
}

double CCreature::GetUserPremiumSpoilBonus() {
    L2_GUARD_FUNCTION();
    return IsPC() ? F2PConfig::GetSpoilBoostRate(CastUser()) : 0.0;
}

double CCreature::GetUserPremiumDropBonus() {
    L2_GUARD_FUNCTION();
    return IsPC() ? F2PConfig::GetNormalItemDropBoostRate(CastUser()) : 0.0;
}

double CCreature::GetUserPremiumVitalItemDropBonus() {
    L2_GUARD_FUNCTION();
    return IsPC() ? F2PConfig::GetVitalItemDropBoostRate(CastUser()) : 0.0;
}

int CCreature::GetUserDropRateByRune(int runeType) {
    L2_GUARD_FUNCTION();
    if (!IsPC() && !IsSummonOrPet()) {
        return 100;
    }

    int bonus = 0;
    if (IsPC()) {
        bonus = CastUser()->GetDropRateByRune(runeType);
    } else if (IsSummonOrPet()) {
        User* const user = GetUserOrMaster();
        if (user != nullptr) {
            bonus = user->GetDropRateByRune(runeType);
        }
    }
    return runtime::AddInt32Wrapping(bonus, 100);
}

int CCreature::GetUserDropAmountByRune(int runeType) {
    L2_GUARD_FUNCTION();
    if (!IsPC() && !IsSummonOrPet()) {
        return 100;
    }

    int bonus = 0;
    if (IsPC()) {
        bonus = CastUser()->GetDropAmountByRune(runeType);
    } else if (IsSummonOrPet()) {
        User* const user = GetUserOrMaster();
        if (user != nullptr) {
            bonus = user->GetDropAmountByRune(runeType);
        }
    }
    return runtime::AddInt32Wrapping(bonus, 100);
}

bool CCreature::HaveSkill(int skillId, int level, int subLevel) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    return _HaveSkill(skillId, level, subLevel);
}

bool CCreature::HaveSkill(int skillId, int level) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    return _HaveSkill(skillId, level);
}

int CCreature::GetSkillLevel(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    const auto found = m_AcquiredSkills.find(skillId);
    return found == m_AcquiredSkills.end() ? -1 : found->second.m_nLevel;
}

int CCreature::GetSkillSubLevel(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    const auto found = m_AcquiredSkills.find(skillId);
    return found == m_AcquiredSkills.end() ? -1 : found->second.m_nSubLevel;
}

AcquiredSkillInfo CCreature::GetAcquiredSkillInfo(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AcquiredSkillsSect);
    const auto found = m_AcquiredSkills.find(skillId);
    if (found == m_AcquiredSkills.end()) {
        return AcquiredSkillInfo(skillId, -1, 0, -1, true);
    }
    return AcquiredSkillInfo(skillId, found->second.m_nLevel, found->second.m_nSubLevel, -1, true);
}

std::uint32_t CCreature::SkillEffectResetSkillReuseDelay(const std::vector<int>& skillIds, int reuseDelayLimit) {
    L2_GUARD_FUNCTION();
    const std::uint32_t maximumDelay = m_SkillReuseDelay.ResetSkillReuseDelay(skillIds, reuseDelayLimit);
    if (maximumDelay > 300000U) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return maximumDelay;
}

std::uint32_t CCreature::ResetSkillReuseDelay(int skillId) {
    L2_GUARD_FUNCTION();
    CNullResetSkillReuseDelayFilter filter;
    const std::uint32_t maximumDelay = m_SkillReuseDelay.ResetSkillReuseDelay(skillId, filter);
    if (maximumDelay > 300000U) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return maximumDelay;
}

std::uint32_t CCreature::ResetSkillReuseDelayAll() {
    L2_GUARD_FUNCTION();
    CNullResetSkillReuseDelayFilter filter;
    const std::uint32_t maximumDelay = m_SkillReuseDelay.ResetSkillReuseDelay(filter);
    if (maximumDelay > 300000U) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return maximumDelay;
}

std::uint32_t CCreature::OlympiadResetSkillReuseDelay(int reuseDelayLimitSeconds, std::vector<int>& excludedSkillIds) {
    L2_GUARD_FUNCTION();

    class COlympiadReuseDelayLimitFilter final : public CAcquireSkillResetSkillReuseDelayFilter {
    public:
        COlympiadReuseDelayLimitFilter(CCreature* creature, int reuseDelayLimitSeconds, std::vector<int>& excludedSkillIds) :
            CAcquireSkillResetSkillReuseDelayFilter(creature),
            m_ReuseDelayLimitMSec(reuseDelayLimitSeconds),
            m_ExcludedSkillList(excludedSkillIds) {}

        bool CanDo(int skillId, ReuseDelayData&) override {
            const auto acquired = m_AcquireSkills.find(skillId);
            if (acquired == m_AcquireSkills.end()) {
                return false;
            }

            const CSkillKey skillKey(skillId, acquired->second.m_nLevel, acquired->second.m_nSubLevel);
            const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(skillKey, nullptr);
            if (skillInfo == nullptr || skillInfo->m_nReuseDelay <= 0.0 ||
                skillInfo->m_nReuseDelay >
                    static_cast<double>(runtime::MultiplyInt32Wrapping(m_ReuseDelayLimitMSec, 1000))) {
                return false;
            }

            return std::find(m_ExcludedSkillList.begin(), m_ExcludedSkillList.end(), skillId) == m_ExcludedSkillList.end();
        }

    protected:
        int m_ReuseDelayLimitMSec;
        std::vector<int>& m_ExcludedSkillList;
    };


    if (reuseDelayLimitSeconds <= 0) {
        return 0;
    }

    COlympiadReuseDelayLimitFilter filter(this, reuseDelayLimitSeconds, excludedSkillIds);
    const std::uint32_t maximumDelay = m_SkillReuseDelay.ResetSkillReuseDelay(filter);
    if (maximumDelay > 300000U) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return maximumDelay;
}

std::uint32_t CCreature::SkillEffectReduceSkillReuseDelay(
    const std::deque<int>& skillIds, int reduction, int reuseDelayLimit
) {
    L2_GUARD_FUNCTION();
    const std::uint32_t maximumDelay = m_SkillReuseDelay.ReduceSkillReuuseDelay(skillIds, reduction, reuseDelayLimit);
    if (maximumDelay > 300000U) {
        runtime::raw_atomic::ExchangeI32(&m_lSaveSkillReuseDelay, 1);
    }
    return maximumDelay;
}

void CCreature::AreaCheckForEveryTick(CGeoZone* zone, CGeoSector* sector, int tick) {
    // Current L2Server 0x005f9440. The zone argument belongs to the exact
    // public ABI even though this body consumes the already-resolved sector.
    L2_GUARD_FUNCTION();
    static_cast<void>(zone);

    if (IsPC() && CastUser()->m_DuelUserInfo.IsInPartyDuel()) {
        return;
    }

    const FVector position = GetPos();
    const std::uint16_t fields = static_cast<std::uint16_t>(sector->m_Field);
    const auto hasField = [fields](GeoFieldTypeEnum type) noexcept {
        const std::uint16_t mask = GeoFieldMask[static_cast<int>(type)];
        return (fields & mask) == mask;
    };

    m_AreaMod.m_bWasFrozen = m_data->m_bIsFreezed;
    m_data->m_bIsFreezed = hasField(GEO_FIELD_FREEZE);

    m_AreaMod.m_bWasInPeaceField = m_AreaMod.m_bIsInPeaceField;
    if (IsInVehicle()) {
        SetPeaceZone(true);
    } else if (!hasField(GEO_FIELD_PEACE)) {
        SetPeaceZone(false);
    } else if (tick != 0) {
        SetPeaceZone(CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), position, AREAT_PEACE, true) != nullptr);
    }

    const bool insideTerritoryHeight = static_cast<double>(sector->m_TerritoryMinZ) <= m_data->m_Pos.z &&
        m_data->m_Pos.z <= static_cast<double>(sector->m_TerritoryMaxZ);
    m_AreaMod.m_bWasInBattleField = m_AreaMod.m_bIsInBattleField;
    m_AreaMod.m_bIsInBattleField = !m_AreaMod.m_bIsInPeaceField &&
        (hasField(GEO_FIELD_BATTLE_RESIDENCE) || hasField(GEO_FIELD_BATTLE_PVP)) && insideTerritoryHeight;
    if (!m_AreaMod.m_bIsInBattleField) {
        m_AreaMod.m_bMPCCAsParty = false;
    } else {
        CArea* const area = CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), position, AREAT_BATTLE, true);
        m_AreaMod.m_bMPCCAsParty = area != nullptr && area->m_bMPCCAsParty;
    }

    m_AreaMod.m_bWasInSSQZone = m_AreaMod.m_bIsInSSQZone;
    m_AreaMod.m_bIsInSSQZone = hasField(GEO_FIELD_SSQ) && insideTerritoryHeight;
    m_AreaMod.m_bWasInDragonLair = m_AreaMod.m_bIsInDragonLair;
    m_AreaMod.m_bIsInDragonLair = hasField(GEO_FIELD_DRAGON_LAIR);
    m_AreaMod.m_bWasInPvPZone = m_AreaMod.m_bIsInPvPZone;
    m_AreaMod.m_bIsInPvPZone = m_AreaMod.m_bIsInBattleField && hasField(GEO_FIELD_BATTLE_PVP);

    CheckRelaxType();

    m_AreaMod.m_bWasUnderWater = m_AreaMod.m_bIsUnderWater;
    if (!IsInVehicle() && !IsInShuttle() && hasField(GEO_FIELD_WATER)) {
        if (tick != 0) {
            m_AreaMod.m_bIsUnderWater =
                CAreaDB::inst().IsUnderWater(position, m_AreaMod.m_nWaterSurfaceHeight, static_cast<int>(GetInZoneID()));
        }
        m_AreaMod.m_bIsNearUnderWater = true;
    } else {
        m_AreaMod.m_bIsUnderWater = false;
        m_AreaMod.m_bIsNearUnderWater = false;
    }

    m_AreaMod.m_bIsNearShuttle = hasField(GEO_FIELD_SHUTTLE);
    m_AreaMod.m_bWasInNoSkill = m_AreaMod.m_bIsInNoSkill;
    m_AreaMod.m_bIsInNoSkill = hasField(GEO_FIELD_NO_SKILL);
    m_AreaMod.m_bIsInTrainingZone = hasField(GEO_FIELD_TRAINING_ZONE);
    m_AreaMod.m_bIsInOnlyPvEZone = hasField(GEO_FIELD_ONLY_PVE);

    ReflectWaterEffect();
    ReflectHeightDamage();
    m_data->m_nInPeaceZone = m_AreaMod.m_bIsInPeaceField ? 1 : 0;
    m_data->m_nInBattleField = m_AreaMod.m_bIsInBattleField ? 1 : 0;
    OnAreaCheckForEveryTick(tick);
}

void CCreature::ReflectDayAndNightEffect() {
    // Current L2Server 0x005ee750.
    if (!m_bIsNight) {
        if (!g_L2Time.m_bIsNight) {
            return;
        }
        m_bIsNight = true;
        m_bIsSysMsg = true;
    } else {
        if (g_L2Time.m_bIsNight) {
            return;
        }
        m_bIsNight = false;
        m_bIsSysMsg = true;
    }

    if (IsPC()) {
        CastUser()->ValidateChanged(std::int64_t{VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED});
    } else {
        ValidateChanged(std::int64_t{VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED});
    }
}

void CCreature::CheckOneGiWok() {
    // Current L2Server 0x005ee7d0.
    if (m_sPowerCount != -1 && ++m_sPowerCount > 7) {
        m_dwMPowerStorage = 0;
        m_sPowerCount = -1;
    }
}

void CCreature::CheckHighHPState() {
    // Current L2Server 0x005ee800.
    if (!m_SkillMod.m_bIsHighHPState) {
        return;
    }
    const double maximum = m_data->m_dBaseHP;
    const double current = m_data->m_dHP;
    if (maximum * 0.8 < current && current <= maximum && current != maximum && IsPC()) {
        CastUser()->ValidateChanged(std::int64_t{VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED});
    }
}

void CCreature::CheckLowHPState() {
    // Current L2Server 0x005ee87c.
    if (!m_SkillMod.m_bIsLowHPState) {
        return;
    }

    const double maximum = m_data->m_dBaseHP;
    const double current = m_data->m_dHP;
    const auto notify = [this]() {
        if (IsPC()) {
            CastUser()->ValidateChanged(std::int64_t{VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED});
        } else {
            ValidateChanged(std::int64_t{VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED});
        }
    };

    if (!m_bIsLowHPLevel1 && current <= maximum * 0.3) {
        m_bIsHP1SysMsg = true;
        if (!m_bIsLowHPLevel2) {
            m_bIsHP2SysMsg = true;
            m_bIsLowHPLevel2 = true;
        }
        m_bIsLowHPLevel1 = true;
        notify();
    } else if (!m_bIsLowHPLevel2 && current <= maximum * 0.6) {
        m_bIsHP2SysMsg = true;
        m_bIsLowHPLevel2 = true;
        notify();
    }

    if (m_bIsLowHPLevel2 && current > maximum * 0.6) {
        if (m_bIsLowHPLevel1) {
            m_bIsHP1SysMsg = true;
            m_bIsLowHPLevel1 = false;
        }
        m_bIsHP2SysMsg = true;
        m_bIsLowHPLevel2 = false;
        notify();
    } else if (m_bIsLowHPLevel1 && current > maximum * 0.3) {
        m_bIsHP1SysMsg = true;
        m_bIsLowHPLevel1 = false;
        notify();
    }
}

void CCreature::FearAction(const bool moveAwayFromSource) {
    // Current L2Server 0x005faf78.
    L2_GUARD_FUNCTION();
    if (IsPC()) {
        User* const user = CastUser();
        CFishingController* const fishing = user != nullptr ? user->m_pFishingController : nullptr;
        if (fishing != nullptr && (fishing->m_FishingState != FISHINGS_NONE || fishing->m_bIsAutoFishing)) {
            StopMove();
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
            user->GetController()->DoAutoFishing(false);
            return;
        }
    }

    if (m_data->m_StopMode == STP_SIT) {
        GetController()->Stand(0x9f6);
        return;
    }
    if (m_data->m_StopMode == STP_STORE) {
        StopStore(true, false);
        return;
    }

    FVector destination = m_vSrcPos;
    if (moveAwayFromSource) {
        const FVector current = m_data->m_Pos;
        const double dx = current.x - m_vSrcPos.x;
        const double dy = current.y - m_vSrcPos.y;
        const double dz = current.z - m_vSrcPos.z;
        const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double unitX = length >= 0.00001 ? dx / length : 1.0;
        const double unitY = length >= 0.00001 ? dy / length : 1.0;
        const double unitZ = length >= 0.00001 ? dz / length : 1.0;

        bool found = false;
        for (int divisor = 1; divisor <= 5; ++divisor) {
            const double distance = static_cast<double>(400 / divisor);
            FVector candidate(current.x + distance * unitX, current.y + distance * unitY, current.z + distance * unitZ);
            const int currentZ = runtime::Win64CvttDoubleToInt32(current.z);
            const int ground = static_cast<std::int16_t>(CWorld::Inst().GetGeo()->FindGround(
                runtime::Win64CvttDoubleToInt32(candidate.x), runtime::Win64CvttDoubleToInt32(candidate.y),
                currentZ + 300
            ));
            if (currentZ - 300 <= ground && ground <= currentZ + 300) {
                candidate.z = static_cast<double>(ground);
                if (CWorld::Inst().IsValidPosition(
                        runtime::Win64CvttDoubleToInt32(candidate.x), runtime::Win64CvttDoubleToInt32(candidate.y)
                    )) {
                    destination = candidate;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return;
        }
    }

    MoveToLocation(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), UsePathFind
    );
}

void CCreature::GeoFieldEffect(CGeoZone* zone, CGeoSector* sector, const int tick, const FVector& position) {
    // Current L2Server 0x005f6ef8.
    if (zone == nullptr || sector == nullptr) {
        return;
    }

    const std::uint16_t fields = static_cast<std::uint16_t>(sector->m_Field);
    const auto hasField = [fields](GeoFieldTypeEnum type) noexcept {
        const std::uint16_t mask = GeoFieldMask[static_cast<std::size_t>(type)];
        return (fields & mask) == mask;
    };
    const bool statusChange = hasField(GEO_FIELD_STATUS_CHANGE);
    const bool instant = hasField(GEO_FIELD_INSTANT);

    m_AreaMod.m_bWasInSwamp = m_AreaMod.m_bIsInSwamp;
    if (tick > 1) {
        CArea* swamp = nullptr;
        if (statusChange) {
            swamp = CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), position, AREAT_SWAMP, true);
        }
        if (swamp != nullptr) {
            swamp->Process(this);
        } else {
            m_AreaMod.m_bIsInSwamp = false;
            m_AreaMod.m_dSpeedModByArea = 1.0;
        }
    }

    const bool swampChanged = m_AreaMod.m_bWasInSwamp != m_AreaMod.m_bIsInSwamp;
    if (swampChanged) {
        CCreature* changed = nullptr;
        std::int64_t parameterMask = 0;
        if (IsPC()) {
            changed = CastUser();
            parameterMask = PCB_USER | PCB_CHAR_ENV;
        } else if (IsNPC()) {
            changed = CastNPC();
            parameterMask = NPCT_GEOFIELDEFFECT;
        }
        if (changed != nullptr) {
            changed->ValidateOrgSpeed();
            changed->ParamChanged(parameterMask);
        }
    }

    m_AreaMod.m_bWasInDamage = m_AreaMod.m_bIsInDamage;
    m_AreaMod.m_bWasInPoison = m_AreaMod.m_bIsInPoison;
    m_AreaMod.m_bWasInInstantSkill = m_AreaMod.m_bIsInInstantSkill;
    if (tick > 1 && IsPC()) {
        if (instant) {
            m_AreaMod.m_bIsInDamage = CAreaDB::inst().FindAllArea(position, AREAT_DAMAGE) != nullptr;
            m_AreaMod.m_bIsInPoison = CAreaDB::inst().FindAllArea(position, AREAT_POISON) != nullptr;
            m_AreaMod.m_bIsInInstantSkill = CAreaDB::inst().FindAllArea(position, AREAT_INSTANT_SKILL) != nullptr;
        } else {
            m_AreaMod.m_bIsInDamage = false;
            m_AreaMod.m_bIsInPoison = false;
            m_AreaMod.m_bIsInInstantSkill = false;
        }
    }

    const bool statusChanged = swampChanged || m_AreaMod.m_bWasInDamage != m_AreaMod.m_bIsInDamage ||
        m_AreaMod.m_bWasInPoison != m_AreaMod.m_bIsInPoison ||
        m_AreaMod.m_bWasInInstantSkill != m_AreaMod.m_bIsInInstantSkill;
    if (IsPC() && statusChanged) {
        CastUser()->SendETCBuffStatusInfo();
        if (AreaBasedRule::CastBadSkill(this)) {
            const bool enabled = m_AreaMod.m_bIsInDamage || m_AreaMod.m_bIsInPoison || m_AreaMod.m_bIsInSwamp ||
                m_AreaMod.m_bIsInInstantSkill;
            CastUser()->SendCompassRegionCode(CRCE_DEBUFF, enabled, false);
        }
    }

    if (IsPC() && m_nTickCount % 5 == 0) {
        m_AreaMod.m_bWasUnderMotherTree = m_AreaMod.m_bIsUnderMotherTree;
        if (tick > 1) {
            CArea* motherTree = nullptr;
            if (statusChange) {
                motherTree = CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), position, AREAT_MOTHER_TREE, true);
            }
            const std::size_t race = static_cast<std::size_t>(m_data->m_Race);
            if (motherTree != nullptr && motherTree->m_vAffectRace[race]) {
                m_AreaMod.m_bIsUnderMotherTree = true;
                if (!m_AreaMod.m_bWasUnderMotherTree) {
                    CastUser()->GetSocket()->Send("chc", 0x62, motherTree->m_nEnteringMessageNO, 0);
                    CastUser()->SendCompassRegionCode(CRCE_BUFF, true, false);
                }
                motherTree->Process(this);
            } else {
                m_AreaMod.m_bIsUnderMotherTree = false;
                if (m_AreaMod.m_bWasUnderMotherTree) {
                    CastUser()->GetSocket()->Send("chc", 0x62, m_AreaMod.m_nLeavingMessageNO, 0);
                    CastUser()->SendCompassRegionCode(CRCE_BUFF, false, false);
                }
                m_AreaMod.m_dMpRegenAreaBonus = 0.0;
                m_AreaMod.m_dHpRegenAreaBonus = 0.0;
            }
        }
    }

    if (instant) {
        CAreaDB::inst().ProcessInstantAreas(this, position);
    }
}

void CCreature::CheckCubicExpired() {
    // Current L2Server 0x00600e0c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_spriteLock);
    bool erased = false;
    auto cubic = m_cubicMap.begin();
    while (cubic != m_cubicMap.end()) {
        if (CCubicHandler::IsExpired(cubic->second.first, cubic->second.second)) {
            cubic = m_cubicMap.erase(cubic);
            erased = true;
        } else {
            ++cubic;
        }
    }
    if (!erased) {
        return;
    }
    if (IsPC()) {
        ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_CUBIC});
    } else {
        ParamChanged();
    }
    m_data->SetNeedUpdatePacket(0x4a);
}

void CCreature::CubicActionTimer() {
    // Current L2Server 0x0061474c.
    L2_GUARD_FUNCTION();
    ::std::map<int, std::pair<CCubic*, CubicActivationInfo>> snapshot;
    {
        L2_GUARD(m_spriteLock);
        if (m_cubicMap.empty()) {
            return;
        }
        snapshot = m_cubicMap;
    }

    for (auto& [slot, cubicEntry] : snapshot) {
        CCubic* const cubic = cubicEntry.first;
        CubicActivationInfo& activation = cubicEntry.second;
        if (cubic == nullptr || !CCubicHandler::OnTick(*this, *cubic, activation, activation.m_Normal)) {
            continue;
        }

        L2_GUARD(m_spriteLock);
        const auto current = m_cubicMap.find(slot);
        if (current != m_cubicMap.end() && current->second.second.summonedTime == activation.summonedTime) {
            current->second.second = activation;
        }
    }
}

void CCreature::ReflectHeightDamage() {
    // Current L2Server 0x005f7754.
    double damage = m_AreaMod.m_dHeightDamage;
    if (damage <= 0.0) {
        return;
    }

    m_AreaMod.m_dHeightDamage = 0.0;
    if (m_AreaMod.m_bIsUnderWater) {
        damage *= 0.1;
    }
    if (IsPC() && static_cast<std::int32_t>(m_data->m_moveType) != CMT_FLY && !m_AreaMod.m_bIsUnderWater) {
        GotDamageByEnvironment(damage, false, DAMAGE_HEIGHT, true);
        SendSystemMessage_d(SystemMessageId::RECEIVED_FALLING_DAMAGE, runtime::Win64CvttDoubleToInt32(damage));
    }
}

void CCreature::ReflectWaterEffect() {
    CSharedCreatureData* const data = m_data;
    std::int32_t environment = ENV_GROUND;

    if (m_AreaMod.m_bWasUnderWater) {
        if (m_AreaMod.m_bIsUnderWater) {
            return;
        }
        environment = static_cast<std::int32_t>(data->m_moveType) == CMT_FLY ? ENV_AIR : ENV_GROUND;
    } else {
        if (!m_AreaMod.m_bIsUnderWater) {
            return;
        }
        environment = ENV_UNDERWATER;
    }

    data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(data->m_Environment), environment, 0x53
    );

    CCreature* affectedCreature = nullptr;
    std::int64_t changedParameters = 0;
    if (IsPC()) {
        affectedCreature = CastUser();
        changedParameters = PCB_USER | PCB_CHAR_ENV;
    } else if (IsNPC()) {
        affectedCreature = CastNPC();
        changedParameters = NPCT_REFECTWATEREFFECT;
    }
    if (affectedCreature == nullptr) {
        return;
    }

    affectedCreature->ValidateOrgSpeed();
    affectedCreature->ParamChanged(changedParameters);
}

void CCreature::ParamChanged(std::int64_t) {}

void CCreature::ParamChanged() { m_bParamChanged.exchange(1, std::memory_order_seq_cst); }

void CCreature::ValidateChanged(int parameterMask) {
    m_nValidateChanged |= parameterMask;
    m_bValidateChanged = true;
}

void CCreature::ValidateClear() {
    runtime::raw_atomic::ExchangeI32(&m_nValidateChanged, 0);
    m_bValidateChanged = false;
}

void CCreature::ValidateExChanged(int parameterMask) {
    m_nValidateExChanged |= parameterMask;
    m_bValidateExChanged = true;
}

void CCreature::ValidateExClear() {
    runtime::raw_atomic::ExchangeI32(&m_nValidateExChanged, 0);
    m_bValidateExChanged = false;
}

void CCreature::ValidateExParameters(int) {}

bool CCreature::CanNormalAttack() const { return true; }

void CCreature::OnAreaCheckForEveryTick(int) {}

void CCreature::OnLevelChanged(int) {}

void CCreature::ChangeTalkTarget(CObject* target) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_spTalkTarget = target->GetSmartID();
    }
}

void CCreature::OnUseSkillFinished(int, int, int, int, int) {}

void CCreature::OnUseSkillStarted(int, int, int, int) {}

void CCreature::ActionFailed(int) {}

void CCreature::ChangeTarget(CObject* target, ChangeTargetReason reason) {
    // Current L2Server 0x006086fc.
    L2_GUARD_FUNCTION();

    if (m_SkillMod.m_BlockTarget) {
        if (IsPC()) {
            CastUser()->GetSocket()->Send("cd", 0xb9, 0);
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }
    if (m_SkillDurationMod.m_TargetMeCount > 0) {
        if (IsPC()) {
            CastUser()->GetSocket()->Send("cd", 0xb9, 0);
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }
    if (target != nullptr && target != this && !target->IsTargetable()) {
        if (IsPC()) {
            CastUser()->GetSocket()->Send("cd", 0xb9, 0);
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }
    if (target != nullptr && !IsInAirShip() && target->IsInAirShip()) {
        if (IsPC()) {
            CastUser()->GetSocket()->Send("cd", 0xb9, 0);
        }
        // ActionFailed(0)
        ActionFailed(AF_DEFAULT);
        return;
    }

    const std::uint32_t newTargetId = target == nullptr ? 0U : target->m_nId;
    const CSmartID oldTarget = m_spTarget;
    if (runtime::runtime_timing::Matches(this, target)) {
        runtime::runtime_timing::Record(
            "target_change", this, target,
            "old_sid=%u new_sid=%u reason=%d current_action=%d "
            "next_action=%d",
            oldTarget.Sid(), newTargetId, static_cast<int>(reason),
            static_cast<int>(GetController()->GetCurrentActionType()), static_cast<int>(GetController()->GetNextActionType())
        );
    }
    if (oldTarget.IsValid() && oldTarget.Sid() != newTargetId && reason != CTR_BY_USER) {
        GetController()->LostTarget(static_cast<int>(oldTarget.Sid()), false);
        if (g_bMoveToLeader && !IsPCOrSummon()) {
            AbandonAttackLeaderOf(oldTarget.GetCreature());
        }
    }

    if (target == nullptr) {
        m_spTarget = nullptr;
        m_data->m_TargetIndex = 0;

        const FVector position = m_data->m_Pos;
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        if (!IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighborDist(
                this, 0x5000, position, 0x800, "cddddd", 0x24, static_cast<int>(m_nId), x, y, z, static_cast<int>(reason)
            );
        } else if (IsPC()) {
            CastUser()->GetSocket()->Send("cddddd", 0x24, static_cast<int>(m_nId), x, y, z, static_cast<int>(reason));
        }
        return;
    }

    m_spTarget = target->GetSmartID();
    m_data->m_TargetIndex = target->GetIndex();
    m_data->m_TargetSID = target->m_nId;

    if (IsPC()) {
        std::int16_t levelDifference = 0;
        if (target->IsNPC()) {
            const std::int16_t targetLevel = static_cast<std::int16_t>(target->CastCreature()->Level());
            levelDifference = static_cast<std::int16_t>(static_cast<std::int16_t>(Level()) - targetLevel);
        }
        CastUser()->GetSocket()->Send(
            "cddhd", 0xb9, 1, static_cast<int>(target->m_nId), static_cast<int>(levelDifference), static_cast<int>(reason)
        );
    }

    if (!IsGMInvisible()) {
        const FVector position = m_data->m_Pos;
        g_ptlUserMap->BroadcastToNeighborExceptSelf(
            this, 0x5000, position, "cdddddd", 0x23, static_cast<int>(m_nId), static_cast<int>(target->m_nId),
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), static_cast<int>(reason)
        );
    }
}

int CCreature::GetBroadcastDist(const int distance) const { return distance; }

void CCreature::SendFreezeStatusMessage() {}

bool CCreature::ValidateStorageSlot() { return true; }

void CCreature::ValidateAbnormalResist() {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* data = m_data;
    const double physicalCandidate = ConBonusF() * static_cast<double>(data->m_OriginAbnormalResist.m_N[0]) *
            m_SkillMod.m_Mods[CCreatureSkillMod::PAbnormalResist].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::PAbnormalResist].m_Diff;
    data->m_AbnormalResist.m_N[0] = runtime::Win64CvttDoubleToInt32(physicalCandidate);

    data = m_data;
    const double magicalCandidate = MenBonusF() * static_cast<double>(data->m_OriginAbnormalResist.m_N[1]) *
            m_SkillMod.m_Mods[CCreatureSkillMod::MAbnormalResist].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::MAbnormalResist].m_Diff;
    data->m_AbnormalResist.m_N[1] = runtime::Win64CvttDoubleToInt32(magicalCandidate);
}

void CCreature::ClampMin(VALIDATE::Enum type) {
    L2_GUARD_FUNCTION();
    const std::uint32_t index = static_cast<std::uint32_t>(type);
    if (index < VALIDATE::MAX) {
        const double minimum = m_ParamMinValue[index];
        double& parameter = m_data->m_dParam[index];
        if (parameter <= minimum && minimum != parameter) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(parameter, minimum, 59);
        }
    }
}

bool CCreature::ValidateBaseData() {
    // Current L2Server 0x005fcd84.
    L2_GUARD_FUNCTION();

    CStats& original = m_data->m_OriginalStat;
    if (!original.IsValid()) {
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreatureName:%s, invalid original stats!!!",
        //     L"Creature_server.cpp",
        //     0x1fb1,
        //     name)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Original stats for %s, object %u, are outside 0..200. Reporting values and clamping them before
        //     recalculation.", name, m_nId)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Original stats for %s, object %u, are outside 0..200. Reporting values and clamping them before
        //     recalculation.", name, m_nId)
        L2_LOG_ERROR(
            Log,
            u"[stats] Original stats for %s, object %u, are outside 0..200; reporting values and clamping them before "
            u"recalculation.",
            name, m_nId
        );
        original.Report(const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1fb2, Utf16ToWide(GetName()).data());
        original.Fix();
    }

    CStats stats = original + m_SkillMod.m_HennaBonus + m_SkillDurationMod.m_StatBonus;
    ValidateAbnormalResist();

    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (item &&
            ((0 < item->m_itemOption.itemOptionKey.variationOption1 &&
                 0 < item->m_itemOption.itemOptionKey.variationOption2) ||
                item->m_pEnchantOption != nullptr) &&
            (ST2SBT[slot] & item->m_data->m_Slot) != 0) {
            item->ApplyStatBonus(stats);
        }
    }

    if (m_nSetItemClassId != 0) {
        CSetItem* const setItem = g_ObjectDB.GetSetItemBySetItemId(m_nSetItemClassId);
        if (setItem != nullptr) {
            stats += setItem->m_Stat;
            stats.GuaranteeMax(1);

            const unsigned int additionalSlot = setItem->m_nAdditionalItemSlot;
            if (additionalSlot != 0 && additionalSlot < ST_MAX) {
                const int itemIndex = m_data->m_nSlotIndex[additionalSlot];
                if (itemIndex != -1) {
                    CItem* const item = m_inv.GetByIndex(itemIndex);
                    if (item != nullptr) {
                        const int classId = item->m_data->m_nClassID;
                        if (setItem->SlotToSetItemClassID(static_cast<int>(additionalSlot), classId) == classId) {
                            stats += setItem->m_StatAdd;
                            stats.GuaranteeMax(1);
                        }
                    }
                }
            }
        }
    }

    m_SkillMod.m_bAppliedStatUpAtNight = false;
    if (!stats.IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreatureName:%s, invalid stats!!!",
        //     L"Creature_server.cpp",
        //     0x1ff1,
        //     GetName())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Calculated stats for %s, object %u, are outside 0..200. Reporting the result and bonuses, then
        //     clamping before publication.", GetName(), m_nId)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Calculated stats for %s, object %u, are outside 0..200. Reporting the result and bonuses, then
        //     clamping before publication.", GetName(), m_nId)
        L2_LOG_ERROR(
            Log,
            u"[stats] Calculated stats for %s, object %u, are outside 0..200; reporting the result and bonuses, then "
            u"clamping before publication.",
            GetName(), m_nId
        );
        stats.Report(const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1ff2, Utf16ToWide(GetName()).data());
        original.Report(const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1ff3, Utf16ToWide(GetName()).data());
        m_SkillMod.m_HennaBonus.Report(const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1ff4, Utf16ToWide(GetName()).data());
        m_SkillDurationMod.m_StatBonus.Report(
            const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1ff5, Utf16ToWide(GetName()).data()
        );
        m_SkillDurationMod.m_StatBonusLimited.Report(
            const_cast<wchar_t*>(L"Creature_server.cpp"), 0x1ff6, Utf16ToWide(GetName()).data()
        );
        stats.Fix();
    }
    m_data->SetStats(stats);
    return true;
}

bool CCreature::ValidateBaseAttributeData() {
    // Current L2Server 0x005fd1b8.
    L2_GUARD_FUNCTION();

    m_data->m_BaseAttribute.Init();
    bool noItem = true;
    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (!item) {
            continue;
        }
        noItem = false;
        if ((ST2SBT[slot] & item->m_data->m_Slot) != 0) {
            m_data->m_BaseAttribute += item->GetTotalAttribute();
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
        }
    }
    if (noItem) {
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
    }

    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        m_data->m_BaseAttribute += npc->m_pClassConst->m_BaseAttribute;
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
    }
    if (IsPC()) {
        User* const user = CastUser();
        if (user->m_pPolymorphNPC != nullptr) {
            m_data->m_BaseAttribute += user->m_pPolymorphNPC->m_pClassConst->m_BaseAttribute;
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
        }
    }
    if (m_data->m_TAttackType >= 0) {
        m_data->m_NeedUpdatePacketBitSet.SetShortValue_IfModifed(
            m_data->m_BaseAttribute.m_nAttackType, static_cast<std::int16_t>(m_data->m_TAttackType), 0x24
        );
    }
    return true;
}

bool CCreature::ValidateSkillMod() {
    // Current L2Server 0x0060cb00.
    L2_GUARD_FUNCTION();

    std::vector<CSkillInfo::PumpSkillData> pumpSkillData;
    {
        L2_GUARD(m_SkillListSect);

        const bool collectPassive = !IsPC() || CastUser()->IsTransformed(TransformType::PureStat) == nullptr;
        if (collectPassive) {
            for (const CSkillInfo* skillInfo : m_PassiveSkills) {
                if (skillInfo != nullptr && IsLockedSkill(skillInfo->m_nSkillId) == 0 && skillInfo->CanUsedBy(this, false) &&
                    skillInfo->CheckOperateCond(this, this, CSkillInfo::OCT_PASSIVE) == -2) {
                    const_cast<CSkillInfo*>(skillInfo)->CollectPumpEffects(&pumpSkillData);
                }
            }
        }

        for (const auto& toggle : m_ToggleSkills) {
            if (IsLockedSkill(toggle.first) == 0 && toggle.second.second && toggle.second.first != nullptr) {
                const_cast<CSkillInfo*>(toggle.second.first)->CollectPumpEffects(&pumpSkillData);
            }
        }
        for (const auto& aura : m_AuraSkills) {
            if (IsLockedSkill(aura.first) == 0 && aura.second.second && aura.second.first != nullptr) {
                const_cast<CSkillInfo*>(aura.second.first)->CollectPumpEffects(&pumpSkillData);
            }
        }
    }

    {
        L2_GUARD(m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : m_InfectedSkills) {
            CSkillInfo* const skillInfo = infected.pSkillInfo;
            if (skillInfo == nullptr || (IsPCOrSummon() && IsLockedBuff(skillInfo->m_nSkillId))) {
                continue;
            }

            const CSkillInfo::SkillEffects* effects = skillInfo->GetPumpEffects(CSkillInfo::EAT_SUCCESS);
            if (effects == nullptr) {
                return true;
            }
            if (effects->empty()) {
                effects = skillInfo->GetPumpEffects(IsPCOrSummon() ? CSkillInfo::EAT_PVP : CSkillInfo::EAT_PVE);
            }

            for (CSkillEffect* effect : *effects) {
                if (effect != nullptr) {
                    pumpSkillData.push_back(
                        CSkillInfo::PumpSkillData{
                            effect, skillInfo, static_cast<std::int32_t>(infected.nSourceCreatureId), 0}
                    );
                }
            }
            for (CSkillEffect* effect : skillInfo->m_vAuraEffect) {
                if (effect != nullptr) {
                    pumpSkillData.push_back(
                        CSkillInfo::PumpSkillData{
                            effect, skillInfo, static_cast<std::int32_t>(infected.nSourceCreatureId), 0}
                    );
                }
            }
        }
    }

    {
        L2_GUARD(m_OptionSkillSect);
        for (const CSkillInfo* skillInfo : m_OptionSkills) {
            if (skillInfo != nullptr && skillInfo->CanUsedBy(this, false)) {
                const_cast<CSkillInfo*>(skillInfo)->CollectPumpEffects(&pumpSkillData);
            }
        }
    }

    if (IsNPC()) {
        {
            L2_GUARD(*m_lock);
            m_InitializingSkillMod.store(1, std::memory_order_seq_cst);
        }
        m_SkillMod.m_bakModSpeed = m_SkillMod.m_Mods[CCreatureSkillMod::Speed];
        m_SkillMod.m_bakModAttackSpeed = m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed];
    }

    InitializeSkillMod();
    for (const CSkillInfo::PumpSkillData& pump : pumpSkillData) {
        CSkillEffect* const effect = pump.m_pSkillEffect;
        if (effect == nullptr || !effect->CheckPumpCondition(this)) {
            continue;
        }
        bool conditionsMet = true;
        for (CEffectCondition* condition : effect->effectCondList) {
            if (!condition->CheckEffectCondition(this)) {
                conditionsMet = false;
                break;
            }
        }
        if (conditionsMet) {
            effect->Pump(this, pump.m_pSkillInfo, pump.m_SourceId);
        }
    }

    if (IsNPC()) {
        int initializing = 0;
        {
            L2_GUARD(*m_lock);
            initializing = m_InitializingSkillMod.exchange(0, std::memory_order_seq_cst);
        }
        if (initializing == 1) { /* two current calls */
            if (m_SkillMod.m_bakModSpeed != m_SkillMod.m_Mods[CCreatureSkillMod::Speed]) {
                CastNPC()->ParamChanged(NPCT_PUMP_SPEED);
            }
            if (m_SkillMod.m_bakModAttackSpeed != m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed]) {
                CastNPC()->ParamChanged(NPCT_PUMP_SPEED2);
            }
        }
    }

    for (int index = 0; index < CCreatureSkillMod::MaxModName; ++index) {
        const double minimum = CCreatureSkillMod::m_ModMinPer[index];
        if (minimum != static_cast<double>(CCreatureSkillMod::MinUndefine) && m_SkillMod.m_Mods[index].m_Per < minimum) {
            m_SkillMod.m_Mods[index].m_Per = minimum;
        }
    }
    m_bIsHP1SysMsg = false;
    m_bIsHP2SysMsg = false;
    return true;
}

void CCreature::ValidateParameters(int parameter, bool force) {
    // Current L2Server 0x0060d248.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    std::uint32_t flags = static_cast<std::uint32_t>(parameter);
    if ((flags & static_cast<std::uint32_t>(VB_MAX)) == 0) {
        if ((flags & static_cast<std::uint32_t>(VB_BASEDATA)) != 0) {
            ValidateBaseData();
        }
        if ((flags & static_cast<std::uint32_t>(VB_BASEATTR | VB_SKILLMOD)) != 0) {
            ValidateBaseAttributeData();
        }
        if ((flags & static_cast<std::uint32_t>(VB_LEVEL)) != 0) {
            flags |= ValidateLevel() ? static_cast<std::uint32_t>(VB_WEAPONARMOR | VB_HPMPCP | VB_SIM)
                                     : static_cast<std::uint32_t>(VB_HPMPCP);
        }
        if ((flags & static_cast<std::uint32_t>(VB_ENV)) != 0) {
            ValidateOrgSpeed();
        }
        if ((flags & static_cast<std::uint32_t>(VB_MOTION)) != 0) {
            flags |= static_cast<std::uint32_t>(VB_HPMPCP | VB_SPEED);
        }
        if ((flags & static_cast<std::uint32_t>(VB_WEAPONARMOR)) != 0) {
            ValidateAllOnWeaponArmors();
        }
        if ((flags & static_cast<std::uint32_t>(VB_ITEMSKILL)) != 0 && ValidateItemSkills()) {
            flags |= static_cast<std::uint32_t>(VB_SKILLLIST | VB_SKILLMOD | VB_SIM);
        }
        if ((flags & static_cast<std::uint32_t>(VB_SKILLLIST)) != 0) {
            ValidateSkillList();
        }
        if ((flags & static_cast<std::uint32_t>(VB_SKILLMOD)) != 0) {
            ValidateSkillMod();
        }
        if ((flags & static_cast<std::uint32_t>(VB_HPMPCP)) != 0) {
            ValidateHPMPCP(force);
        }
        if ((flags & static_cast<std::uint32_t>(VB_SIM)) != 0) {
            ValidateAllOnSIM();
        }
        if ((flags & static_cast<std::uint32_t>(VB_SPEED)) != 0) {
            ValidateOrgSpeed();
        }
        if ((flags & static_cast<std::uint32_t>(VB_LEVELUP)) != 0) {
            ValidateLevelUP();
        }
        if (IsNPC()) {
            CastNPC()->ParamChanged(NPCT_VALIDATE2);
        } else if (IsPC()) {
            ParamChanged(PCB_USER);
        } else {
            ParamChanged();
        }
    } else {
        ValidateBaseData();
        ValidateBaseAttributeData();
        ValidateLevel();
        ValidateOrgSpeed();
        ValidateAllOnWeaponArmors();
        ValidateItemSkills();
        ValidateSkillList();
        ValidateSkillMod();
        ValidateHPMPCP(force);
        ValidateAllOnSIM();
        ValidateOrgSpeed();
        if ((flags & static_cast<std::uint32_t>(VB_LEVELUP)) != 0) {
            ValidateLevelUP();
        }
        if (IsNPC()) {
            CastNPC()->ParamChanged(NPCT_VALIDATE2);
        } else if (IsPC()) {
            ParamChanged(PCB_USER);
        } else {
            ParamChanged();
        }
    }
}

void CCreature::OnGotDamage(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    if (damage <= 0.0) {
        return;
    }
    if (IsInvisibleBySkill()) {
        SetVisibleBySkill(true, true);
    }
    if (attacker != nullptr) {
        TriggerSkillByDamage(*attacker, damage);
    }
}

bool CCreature::ValidateBaseHP() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    if (m_data->m_Race == ::RT_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Race type (RT_NONE) error.",
        //     "Creature_server.cpp",
        //     0x23a8)
        // Log.Add(
        //     LOG_ERROR,
        //     "[stats] Cannot recalculate maximum HP for object %u: race is RT_NONE. Leaving the stored value unchanged.",
        //     m_nId)
        L2_LOG_ERROR(
            Log, "[stats] Cannot recalculate maximum HP for object %u: race is RT_NONE; leaving the stored value unchanged.",
            m_nId
        );
        return false;
    }

    const double constitutionBonus = ConBonusF();
    const double originalHP = m_data->m_dOrgHP;
    const double maximumHPPer = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Per;
    const double maximumHPDiff = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Diff;
    const double armorBonus = EquipedArmorsHpBonus();
    const double maximumHP = armorBonus + constitutionBonus * charismaBonus * originalHP * maximumHPPer + maximumHPDiff;

    if (!FloatChecker::CheckValidation(maximumHP)) {
        const int fpClass = FloatChecker::FPClass(maximumHP);
        const double oldMaximumHP = m_data->m_dBaseHP;
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]1 inval fp input:%.5f(%p) "
        //     L"fpclass:%04x old:%.5f(%p)",
        //     name,
        //     maximumHP,
        //     DoubleBitsAsPointer(maximumHP),
        //     fpClass,
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) is not finite.
        //     keeping %.17g (bits %p).", name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP),
        //     static_cast<unsigned>(fpClass),
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        // L2_LOG_ERROR(Log,
        //     L"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) is not finite.
        //     keeping %.17g (bits %p).", name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP),
        //     static_cast<unsigned>(fpClass),
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        L2_LOG_ERROR(
            Log,
            u"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) is not finite; "
            u"keeping %.17g (bits %p).",
            name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP), static_cast<unsigned>(fpClass), oldMaximumHP,
            DoubleBitsAsPointer(oldMaximumHP)
        );

        const double diagnosticDiff = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Diff;
        const double diagnosticPer = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Per;
        const double diagnosticOriginalHP = m_data->m_dOrgHP;
        const double diagnosticArmorFirst = EquipedArmorsHpBonus();
        const double diagnosticArmorSecond = EquipedArmorsHpBonus();
        const double diagnosticConFirst = ConBonusF();
        const double diagnosticConSecond = ConBonusF();
        const char16_t* const diagnosticName = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]2 inval fp org:%.5f(%p) con:%d "
        //     L"c_bonus:%.5f(%p) m_per:%.5f(%p) "
        //     L"m_dif:%.5f(%p) arm:%.5f(%p)",
        //     diagnosticName,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorFirst))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Non-finite maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1
        //     %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor
        //     bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConFirst,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorFirst,
        //     DoubleBitsAsPointer(diagnosticArmorFirst),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorSecond),
        //     charismaBonus)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Non-finite maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1
        //     %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor
        //     bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConFirst,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorFirst,
        //     DoubleBitsAsPointer(diagnosticArmorFirst),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorSecond),
        //     charismaBonus)
        L2_LOG_ERROR(
            Log,
            u"[stats] Non-finite maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1 "
            u"%.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor "
            u"bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p); CHA multiplier used %.17g.",
            diagnosticName, m_nId, diagnosticOriginalHP, DoubleBitsAsPointer(diagnosticOriginalHP), m_data->m_Stat.m_N[2],
            diagnosticConFirst, DoubleBitsAsPointer(diagnosticConFirst), diagnosticConSecond,
            DoubleBitsAsPointer(diagnosticConSecond), diagnosticPer, DoubleBitsAsPointer(diagnosticPer), diagnosticDiff,
            DoubleBitsAsPointer(diagnosticDiff), diagnosticArmorFirst, DoubleBitsAsPointer(diagnosticArmorFirst),
            diagnosticArmorSecond, DoubleBitsAsPointer(diagnosticArmorSecond), charismaBonus
        );
        return false;
    }

    if (2147483647.0 < maximumHP) {
        const int fpClass = FloatChecker::FPClass(maximumHP);
        const double oldMaximumHP = m_data->m_dBaseHP;
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]1 huge MAX HP new:%.5f(%p) "
        //     L"fpclass:%04x old:%.5f(%p)",
        //     name,
        //     maximumHP,
        //     DoubleBitsAsPointer(maximumHP),
        //     fpClass,
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) exceeds 2147483647.
        //     keeping %.17g (bits %p).", name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP),
        //     static_cast<unsigned>(fpClass),
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        // L2_LOG_ERROR(Log,
        //     L"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) exceeds 2147483647.
        //     keeping %.17g (bits %p).", name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP),
        //     static_cast<unsigned>(fpClass),
        //     oldMaximumHP,
        //     DoubleBitsAsPointer(oldMaximumHP))
        L2_LOG_ERROR(
            Log,
            u"[stats] Maximum HP calculation rejected for %s, object %u: %.17g (bits %p, class 0x%04x) exceeds 2147483647; "
            u"keeping %.17g (bits %p).",
            name, m_nId, maximumHP, DoubleBitsAsPointer(maximumHP), static_cast<unsigned>(fpClass), oldMaximumHP,
            DoubleBitsAsPointer(oldMaximumHP)
        );

        const double diagnosticDiff = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Diff;
        const double diagnosticPer = m_SkillMod.m_Mods[CCreatureSkillMod::MaxHP].m_Per;
        const double diagnosticOriginalHP = m_data->m_dOrgHP;
        const double diagnosticArmorFirst = EquipedArmorsHpBonus();
        const double diagnosticArmorSecond = EquipedArmorsHpBonus();
        const double diagnosticConFirst = ConBonusF();
        const double diagnosticConSecond = ConBonusF();
        const char16_t* const diagnosticName = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]2 huge MAX HP org:%.5f(%p) con:%d "
        //     L"c_bonus:%.5f(%p) m_per:%.5f(%p) "
        //     L"m_dif:%.5f(%p) arm:%.5f(%p)",
        //     diagnosticName,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorFirst))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Excessive maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1
        //     %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor
        //     bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConFirst,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorFirst,
        //     DoubleBitsAsPointer(diagnosticArmorFirst),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorSecond),
        //     charismaBonus)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Excessive maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1
        //     %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor
        //     bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginalHP,
        //     DoubleBitsAsPointer(diagnosticOriginalHP),
        //     m_data->m_Stat.m_N[2],
        //     diagnosticConFirst,
        //     DoubleBitsAsPointer(diagnosticConFirst),
        //     diagnosticConSecond,
        //     DoubleBitsAsPointer(diagnosticConSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     diagnosticArmorFirst,
        //     DoubleBitsAsPointer(diagnosticArmorFirst),
        //     diagnosticArmorSecond,
        //     DoubleBitsAsPointer(diagnosticArmorSecond),
        //     charismaBonus)
        L2_LOG_ERROR(
            Log,
            u"[stats] Excessive maximum HP recheck for %s, object %u: base HP %.17g (bits %p), CON %d, CON bonus read 1 "
            u"%.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g (bits %p), additive bonus %.17g (bits %p), armor "
            u"bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p); CHA multiplier used %.17g.",
            diagnosticName, m_nId, diagnosticOriginalHP, DoubleBitsAsPointer(diagnosticOriginalHP), m_data->m_Stat.m_N[2],
            diagnosticConFirst, DoubleBitsAsPointer(diagnosticConFirst), diagnosticConSecond,
            DoubleBitsAsPointer(diagnosticConSecond), diagnosticPer, DoubleBitsAsPointer(diagnosticPer), diagnosticDiff,
            DoubleBitsAsPointer(diagnosticDiff), diagnosticArmorFirst, DoubleBitsAsPointer(diagnosticArmorFirst),
            diagnosticArmorSecond, DoubleBitsAsPointer(diagnosticArmorSecond), charismaBonus
        );
        return false;
    }

    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dBaseHP, 1.0 <= maximumHP ? maximumHP : 1.0, 8);
    return true;
}

bool CCreature::ValidateBaseMP() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double menBonus = MenBonusF();
    const double originalMP = m_data->m_dOrgMP;
    const double maximumMPPer = m_SkillMod.m_Mods[CCreatureSkillMod::MaxMP].m_Per;
    const double maximumMPDiff = m_SkillMod.m_Mods[CCreatureSkillMod::MaxMP].m_Diff;
    const int armorBonus = EquipedArmorsMpBonus();
    const double maximumMP =
        static_cast<double>(armorBonus) + menBonus * charismaBonus * originalMP * maximumMPPer + maximumMPDiff;
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dBaseMP, 1.0 <= maximumMP ? maximumMP : 1.0, 9);
    return true;
}

bool CCreature::ValidateBaseCP() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    CSharedCreatureData* const data = m_data;
    const double conBonus = ConBonusF();
    const double maximumCP = conBonus * charismaBonus * data->m_dOrgCP * m_SkillMod.m_Mods[CCreatureSkillMod::MaxCP].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::MaxCP].m_Diff;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dBaseCP, 1.0 <= maximumCP ? maximumCP : 1.0, 10);
    return true;
}

bool CCreature::ValidateCPRegen() {
    L2_GUARD_FUNCTION();
    if (m_data->m_Race == ::RT_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Race type (RT_NONE) error.",
        //     "Creature_server.cpp",
        //     0x240e)
        // Log.Add(
        //     LOG_ERROR,
        //     "[stats] Cannot recalculate CP regeneration for object %u: race is RT_NONE. Leaving the stored value
        //     unchanged.", m_nId)
        L2_LOG_ERROR(
            Log,
            "[stats] Cannot recalculate CP regeneration for object %u: race is RT_NONE; leaving the stored value unchanged.",
            m_nId
        );
        return false;
    }

    const double levelBonus = LevelBonus();
    const double originalCPRegen = m_data->m_dOrgCPRegen;
    const double conBonus = ConBonusF();
    CSharedCreatureData* const data = m_data;
    data->m_dCPRegen = (conBonus * levelBonus * originalCPRegen * m_SkillMod.m_Mods[CCreatureSkillMod::CPRegen].m_Per +
                           m_SkillMod.m_Mods[CCreatureSkillMod::CPRegen].m_Diff) *
        m_dCPRegenModByWeight;
    return true;
}

bool CCreature::ValidateHPRegen() {
    L2_GUARD_FUNCTION();
    if (m_data->m_Race == ::RT_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Race type (RT_NONE) error.",
        //     "Creature_server.cpp",
        //     0x2428)
        // Log.Add(
        //     LOG_ERROR,
        //     "[stats] Cannot recalculate HP regeneration for object %u: race is RT_NONE. Leaving the stored value
        //     unchanged.", m_nId)
        L2_LOG_ERROR(
            Log,
            "[stats] Cannot recalculate HP regeneration for object %u: race is RT_NONE; leaving the stored value unchanged.",
            m_nId
        );
        return false;
    }

    const double levelBonus = LevelBonus();
    const double originalHPRegen = m_data->m_dOrgHPRegen;
    const double conBonus = ConBonusF();
    CSharedCreatureData* const data = m_data;
    data->m_dHPRegen = (conBonus * levelBonus * originalHPRegen * m_SkillMod.m_Mods[CCreatureSkillMod::HPRegen].m_Per +
                           m_SkillMod.m_Mods[CCreatureSkillMod::HPRegen].m_Diff) *
        m_dHPRegenModByWeight;
    return true;
}

bool CCreature::ValidateMPRegen() {
    L2_GUARD_FUNCTION();
    if (m_data->m_Race == ::RT_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Race type (RT_NONE) error.",
        //     "Creature_server.cpp",
        //     0x2444)
        // Log.Add(
        //     LOG_ERROR,
        //     "[stats] Cannot recalculate MP regeneration for object %u: race is RT_NONE. Leaving the stored value
        //     unchanged.", m_nId)
        L2_LOG_ERROR(
            Log,
            "[stats] Cannot recalculate MP regeneration for object %u: race is RT_NONE; leaving the stored value unchanged.",
            m_nId
        );
        return false;
    }

    const double levelBonus = LevelBonus();
    const double originalMPRegen = m_data->m_dOrgMPRegen;
    const double menBonus = MenBonusF();
    CSharedCreatureData* const data = m_data;
    data->m_dMPRegen = (menBonus * levelBonus * originalMPRegen * m_SkillMod.m_Mods[CCreatureSkillMod::MPRegen].m_Per +
                           m_SkillMod.m_Mods[CCreatureSkillMod::MPRegen].m_Diff) *
        m_dMPRegenModByWeight;
    return true;
}

bool CCreature::ValidatePAvoidRate() {
    L2_GUARD_FUNCTION();
    int dexterity = m_data->m_Stat.m_N[4];
    if (dexterity < 1) {
        dexterity = 1;
    }
    const int level = Level();
    const int originalAvoid = m_data->m_nOrgParam[VALIDATE::PAVOID];
    const double dexteritySquareRoot = std::sqrt(static_cast<double>(dexterity));
    CSharedCreatureData* const data = m_data;
    const std::int32_t levelAndAvoid = runtime::AddInt32Wrapping(level, originalAvoid);
    double candidate = (dexteritySquareRoot * 5.0 + static_cast<double>(levelAndAvoid)) *
            m_SkillMod.m_Mods[CCreatureSkillMod::PAvoid].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::PAvoid].m_Diff;

    const int currentLevel = data->m_nLevel;
    if (currentLevel >= 70) {
        const std::int32_t excessLevel = runtime::AddInt32Wrapping(currentLevel, -69);
        if (currentLevel < 78) {
            candidate += static_cast<double>(excessLevel);
        } else {
            candidate += static_cast<double>(excessLevel) * 1.21;
        }
    }

    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::PAVOID], candidate, 59);
    ValidateGradePenalty(VALIDATE::PAVOID);
    ClampMin(VALIDATE::PAVOID);
    return true;
}

bool CCreature::ValidateMAvoidRate() {
    L2_GUARD_FUNCTION();
    const double witSquareRoot = std::sqrt(static_cast<double>(m_data->m_Stat.m_N[5]));
    const int level = Level();
    CSharedCreatureData* const data = m_data;
    const std::int32_t levelTwice = runtime::AddInt32Wrapping(level, level);
    const std::int32_t baseAvoid = runtime::AddInt32Wrapping(data->m_nOrgParam[VALIDATE::MAVOID], levelTwice);
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        data->m_dParam[VALIDATE::MAVOID],
        (static_cast<double>(baseAvoid) + witSquareRoot * 3.0) * m_SkillMod.m_Mods[CCreatureSkillMod::MAvoid].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::MAvoid].m_Diff,
        59
    );
    ValidateGradePenalty(VALIDATE::MAVOID);
    ClampMin(VALIDATE::MAVOID);
    return true;
}

bool CCreature::ValidatePHit() {
    L2_GUARD_FUNCTION();
    int dexterity = m_data->m_Stat.m_N[4];
    if (dexterity < 1) {
        dexterity = 1;
    }
    const int level = Level();
    const int originalHit = m_data->m_nOrgParam[VALIDATE::PHIT];
    const double dexteritySquareRoot = std::sqrt(static_cast<double>(dexterity));
    CSharedCreatureData* const data = m_data;
    const std::int32_t levelAndHit = runtime::AddInt32Wrapping(level, originalHit);
    double candidate =
        ((dexteritySquareRoot * 5.0 + static_cast<double>(levelAndHit)) * m_SkillMod.m_Mods[CCreatureSkillMod::PHit].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::PHit].m_Diff) *
            m_SkillMod.m_Mods[CCreatureSkillMod::HitBonusAtNight].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::HitBonusAtNight].m_Diff;

    const int currentLevel = data->m_nLevel;
    if (currentLevel >= 70) {
        const std::int32_t excessLevel = runtime::AddInt32Wrapping(currentLevel, -69);
        if (currentLevel < 78) {
            candidate += static_cast<double>(excessLevel);
        } else {
            candidate += static_cast<double>(excessLevel) * 1.21;
        }
    }

    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::PHIT], candidate, 59);
    ValidateGradePenalty(VALIDATE::PHIT);
    ClampMin(VALIDATE::PHIT);
    return true;
}

bool CCreature::ValidateMHit() {
    L2_GUARD_FUNCTION();
    const double witSquareRoot = std::sqrt(static_cast<double>(m_data->m_Stat.m_N[5]));
    const int level = Level();
    CSharedCreatureData* const data = m_data;
    const std::int32_t levelTwice = runtime::AddInt32Wrapping(level, level);
    const std::int32_t baseHit = runtime::AddInt32Wrapping(data->m_nOrgParam[VALIDATE::MHIT], levelTwice);
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        data->m_dParam[VALIDATE::MHIT],
        (static_cast<double>(baseHit) + witSquareRoot * 3.0) * m_SkillMod.m_Mods[CCreatureSkillMod::MHit].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::MHit].m_Diff,
        59
    );
    ValidateGradePenalty(VALIDATE::MHIT);
    ClampMin(VALIDATE::MHIT);
    return true;
}

bool CCreature::ValidateMCriticalProb() {
    L2_GUARD_FUNCTION();
    const double witBonus = WitBonusF();
    CSharedCreatureData* const data = m_data;
    const double candidate = witBonus * static_cast<double>(data->m_nOrgParam[VALIDATE::MCRITICAL]) *
            m_SkillMod.m_Mods[CCreatureSkillMod::MagicCriticalProb].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::MagicCriticalProb].m_Diff;
    const double upperLimit = m_SkillMod.CriticalMaxUp.m_nMagic + 50.0;
    const double upperClamped = candidate <= upperLimit ? candidate : upperLimit;
    const double criticalProbability = 0.5 <= upperClamped ? upperClamped : 0.5;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::MCRITICAL], criticalProbability, 59);
    ClampMin(VALIDATE::MCRITICAL);
    return true;
}

bool CCreature::ValidatePCriticalProb() {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* const data = m_data;
    const double dexterityBonus = DexBonusF();
    const double candidate = dexterityBonus * static_cast<double>(data->m_nOrgParam[VALIDATE::PCRITICAL]) *
            m_SkillMod.m_Mods[CCreatureSkillMod::CriticalProb].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::CriticalProb].m_Diff;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::PCRITICAL], candidate, 59);
    ValidateGradePenalty(VALIDATE::PCRITICAL);

    const double upperLimit = m_SkillMod.CriticalMaxUp.m_nPhysics + 50.0;
    const double upperClamped = candidate <= upperLimit ? candidate : upperLimit;
    const double criticalProbability = 0.0 <= upperClamped ? upperClamped : 0.0;
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        m_data->m_dParam[VALIDATE::PCRITICAL], criticalProbability, 59
    );
    ClampMin(VALIDATE::PCRITICAL);
    return true;
}

bool CCreature::ValidateOrgPAttack() {
    L2_GUARD_FUNCTION();
    const double weaponValue = EquipedWeaponValidate(VALIDATE::PATTACK);
    const double armorValue = EquipedArmorsValidate(VALIDATE::PATTACK);
    m_data->m_nOrgParam[VALIDATE::PATTACK] = runtime::Win64CvttDoubleToInt32(weaponValue + armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::PATTACK] == 0) {
        data->m_nOrgParam[VALIDATE::PATTACK] = data->m_nBaseParam[VALIDATE::PATTACK];
    }
    ValidatePAttack();
    return true;
}

bool CCreature::ValidateOrgMAttack() {
    L2_GUARD_FUNCTION();
    g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]);
    const double weaponValue = EquipedWeaponValidate(VALIDATE::MATTACK);
    const double armorValue = EquipedArmorsValidate(VALIDATE::MATTACK);
    m_data->m_nOrgParam[VALIDATE::MATTACK] = runtime::Win64CvttDoubleToInt32(weaponValue + armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::MATTACK] == 0) {
        data->m_nOrgParam[VALIDATE::MATTACK] = data->m_nBaseParam[VALIDATE::MATTACK];
    }
    ValidateMAttack();
    return true;
}

bool CCreature::ValidateOrgAttackSpeed() {
    L2_GUARD_FUNCTION();
    const double weaponValue = EquipedWeaponValidate(VALIDATE::PATTACKSPEED);
    const double armorValue = EquipedArmorsValidate(VALIDATE::PATTACKSPEED);
    m_data->m_nOrgParam[VALIDATE::PATTACKSPEED] = runtime::Win64CvttDoubleToInt32(weaponValue + armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::PATTACKSPEED] == 0) {
        data->m_nOrgParam[VALIDATE::PATTACKSPEED] = data->m_nBaseParam[VALIDATE::PATTACKSPEED];
    }
    ValidateAttackSpeed();
    ValidateAttackTime();
    return true;
}

bool CCreature::ValidateOrgPCriticalProb() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PCRITICAL));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PCRITICAL));
    m_data->m_nOrgParam[VALIDATE::PCRITICAL] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::PCRITICAL] == 0) {
        data->m_nOrgParam[VALIDATE::PCRITICAL] = data->m_nBaseParam[VALIDATE::PCRITICAL];
    }
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::PCRITICAL] = m_data->m_nBaseParam[VALIDATE::PCRITICAL];
    }
    ValidatePCriticalProb();
    return true;
}

bool CCreature::ValidateOrgMCriticalProb() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MCRITICAL));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MCRITICAL));
    m_data->m_nOrgParam[VALIDATE::MCRITICAL] =
        runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(weaponValue, armorValue), 5);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::MCRITICAL] = m_data->m_nBaseParam[VALIDATE::MCRITICAL];
    }
    ValidateMCriticalProb();
    return true;
}

bool CCreature::ValidateOrgPHit() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PHIT));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PHIT));
    m_data->m_nOrgParam[VALIDATE::PHIT] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::PHIT] == 0) {
        data->m_nOrgParam[VALIDATE::PHIT] = data->m_nBaseParam[VALIDATE::PHIT];
    }
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::PHIT] = m_data->m_nBaseParam[VALIDATE::PHIT];
    }
    ValidatePHit();
    return true;
}

bool CCreature::ValidateOrgMHit() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MHIT));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MHIT));
    m_data->m_nOrgParam[VALIDATE::MHIT] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    CSharedCreatureData* const data = m_data;
    if (data->m_nOrgParam[VALIDATE::MHIT] == 0) {
        data->m_nOrgParam[VALIDATE::MHIT] = data->m_nBaseParam[VALIDATE::MHIT];
    }
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::MHIT] = m_data->m_nBaseParam[VALIDATE::MHIT];
    }
    ValidateMHit();
    return true;
}

bool CCreature::ValidateOrgPAvoidRate() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PAVOID));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PAVOID));
    m_data->m_nOrgParam[VALIDATE::PAVOID] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::PAVOID] = m_data->m_nBaseParam[VALIDATE::PAVOID];
    }
    ValidatePAvoidRate();
    return true;
}

bool CCreature::ValidateOrgMAvoidRate() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MAVOID));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MAVOID));
    m_data->m_nOrgParam[VALIDATE::MAVOID] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::MAVOID] = m_data->m_nBaseParam[VALIDATE::MAVOID];
    }
    ValidateMAvoidRate();
    return true;
}

bool CCreature::ValidateOrgMUseSpeed() {
    L2_GUARD_FUNCTION();
    const double armorValue = EquipedArmorsValidate(VALIDATE::MATTACKSPEED);
    const double weaponValue = EquipedWeaponValidate(VALIDATE::MATTACKSPEED);
    m_data->m_nOrgParam[VALIDATE::MATTACKSPEED] = runtime::Win64CvttDoubleToInt32(armorValue + weaponValue);
    if (m_data->m_nOrgParam[VALIDATE::MATTACKSPEED] == 0) {
        m_data->m_nOrgParam[VALIDATE::MATTACKSPEED] = 1;
    }
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::MATTACKSPEED] = m_data->m_nBaseParam[VALIDATE::MATTACKSPEED];
    }
    ValidateMUseSpeed();
    return true;
}

bool CCreature::ValidateOrgPDefend() {
    L2_GUARD_FUNCTION();
    const double armorValue = EquipedArmorsValidate(VALIDATE::PDEFEND);
    const double weaponValue = EquipedWeaponValidate(VALIDATE::PDEFEND);
    m_data->m_nOrgParam[VALIDATE::PDEFEND] = runtime::Win64CvttDoubleToInt32(armorValue + weaponValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::PDEFEND] = m_data->m_nBaseParam[VALIDATE::PDEFEND];
    }
    ValidateOrgShieldDefend();
    ValidateOrgShieldDefendRate();
    ValidatePDefend();
    return true;
}

bool CCreature::ValidateOrgMDefend() {
    L2_GUARD_FUNCTION();
    const double armorValue = EquipedArmorsValidate(VALIDATE::MDEFEND);
    const double weaponValue = EquipedWeaponValidate(VALIDATE::MDEFEND);
    m_data->m_nOrgParam[VALIDATE::MDEFEND] = runtime::Win64CvttDoubleToInt32(armorValue + weaponValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::MDEFEND] = m_data->m_nBaseParam[VALIDATE::MDEFEND];
    }
    ValidateMDefend();
    return true;
}

bool CCreature::ValidateOrgShieldDefend() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::SHIELDDEFENSE));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::SHIELDDEFENSE));
    m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSE] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSE] = m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSE];
    }
    ValidateShieldDefend();
    return true;
}

bool CCreature::ValidateOrgShieldDefendRate() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::SHIELDDEFENSERATE));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::SHIELDDEFENSERATE));
    m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSERATE] = runtime::AddInt32Wrapping(weaponValue, armorValue);
    if (IsPC() && CastUser()->IsCombatTransformed() != nullptr && CastUser()->m_transform->IsPureState()) {
        m_data->m_nOrgParam[VALIDATE::SHIELDDEFENSERATE] = m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSERATE];
    }
    ValidateShieldDefendRate();
    return true;
}

bool CCreature::ValidateEnchantExtraBonus() {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < VALIDATE::EXTRA_MAX; ++index) {
        m_dExtraParam[index] = EquipedWeaponExtraValidate(static_cast<VALIDATE::ExtraEnum>(index));
    }
    return true;
}

bool CCreature::ValidatePAttack() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double strengthBonus = StrBonusF();
    const int originalAttack = m_data->m_nOrgParam[VALIDATE::PATTACK];
    const double levelBonus = LevelBonus();
    const double candidate = (((levelBonus * strengthBonus) * static_cast<double>(originalAttack)) * charismaBonus) *
            m_SkillMod.m_Mods[CCreatureSkillMod::PAttack].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::PAttack].m_Diff;
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[VALIDATE::PATTACK], candidate, 59);
    ValidateGradePenalty(VALIDATE::PATTACK);
    ClampMin(VALIDATE::PATTACK);
    return true;
}

bool CCreature::ValidateMAttack() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]);
    const double intelligenceBonusFirst = IntBonusF();
    const int originalAttack = m_data->m_nOrgParam[VALIDATE::MATTACK];
    const double intelligenceBonusSecond = IntBonusF();
    const double levelBonusFirst = LevelBonus();
    const double levelBonusSecond = LevelBonus();
    const double candidate = levelBonusSecond * levelBonusFirst * intelligenceBonusSecond * intelligenceBonusFirst *
            static_cast<double>(originalAttack) * charismaBonus * m_SkillMod.m_Mods[CCreatureSkillMod::MAttack].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::MAttack].m_Diff;
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[VALIDATE::MATTACK], candidate, 59);

    const double validatedAttack = m_data->m_dParam[VALIDATE::MATTACK];
    if (!FloatChecker::CheckValidation(validatedAttack)) {
        const int fpClass = FloatChecker::FPClass(validatedAttack);
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]1 inval MAttack %.5f(%p) fpclass:%04x",
        //     name,
        //     validatedAttack,
        //     DoubleBitsAsPointer(validatedAttack),
        //     fpClass)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Stored magic attack for %s, object %u, is not finite: %.17g (bits %p, class 0x%04x). Continuing
        //     grade-penalty and minimum-value checks.", name, m_nId, validatedAttack, DoubleBitsAsPointer(validatedAttack),
        //     static_cast<unsigned>(fpClass))
        // L2_LOG_ERROR(Log,
        //     L"[stats] Stored magic attack for %s, object %u, is not finite: %.17g (bits %p, class 0x%04x). Continuing
        //     grade-penalty and minimum-value checks.", name, m_nId, validatedAttack, DoubleBitsAsPointer(validatedAttack),
        //     static_cast<unsigned>(fpClass))
        L2_LOG_ERROR(
            Log,
            u"[stats] Stored magic attack for %s, object %u, is not finite: %.17g (bits %p, class 0x%04x); continuing "
            u"grade-penalty and minimum-value checks.",
            name, m_nId, validatedAttack, DoubleBitsAsPointer(validatedAttack), static_cast<unsigned>(fpClass)
        );

        const double diagnosticDiff = m_SkillMod.m_Mods[CCreatureSkillMod::MAttack].m_Diff;
        const double diagnosticPer = m_SkillMod.m_Mods[CCreatureSkillMod::MAttack].m_Per;
        const int diagnosticOriginalAttack = m_data->m_nOrgParam[VALIDATE::MATTACK];
        const double diagnosticLevelFirst = LevelBonus();
        const double diagnosticLevelSecond = LevelBonus();
        const double diagnosticIntFirst = IntBonusF();
        const double diagnosticIntSecond = IntBonusF();
        const char16_t* const diagnosticName = GetName();

        // The Win64 target feeds the original int through a %.5f slot.
        // Convert it so the host variadic ABI keeps the same
        // intended numeric diagnostic without undefined argument decoding.
        const double diagnosticOriginal = static_cast<double>(diagnosticOriginalAttack);
        // Log.Add(
        //     LOG_ERROR,
        //     L"FloatERR [%s]2 inval MAttack org:%.5f(%p) "
        //     L"int_b:%.5f(%p) lvl_b:%.5f(%p) "
        //     L"m_per:%.5f(%p) m_dif:%.5f(%p)",
        //     diagnosticName,
        //     diagnosticOriginal,
        //     DoubleBitsAsPointer(diagnosticOriginal),
        //     diagnosticIntSecond,
        //     DoubleBitsAsPointer(diagnosticIntFirst),
        //     diagnosticLevelSecond,
        //     DoubleBitsAsPointer(diagnosticLevelFirst),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff))
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Magic attack recheck for %s, object %u: base attack %.17g (bits %p), INT bonus read 1 %.17g (bits
        //     %p), read 2 %.17g (bits %p), level bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g
        //     (bits %p), additive bonus %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginal,
        //     DoubleBitsAsPointer(diagnosticOriginal),
        //     diagnosticIntFirst,
        //     DoubleBitsAsPointer(diagnosticIntFirst),
        //     diagnosticIntSecond,
        //     DoubleBitsAsPointer(diagnosticIntSecond),
        //     diagnosticLevelFirst,
        //     DoubleBitsAsPointer(diagnosticLevelFirst),
        //     diagnosticLevelSecond,
        //     DoubleBitsAsPointer(diagnosticLevelSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     charismaBonus)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Magic attack recheck for %s, object %u: base attack %.17g (bits %p), INT bonus read 1 %.17g (bits
        //     %p), read 2 %.17g (bits %p), level bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g
        //     (bits %p), additive bonus %.17g (bits %p). CHA multiplier used %.17g.", diagnosticName, m_nId,
        //     diagnosticOriginal,
        //     DoubleBitsAsPointer(diagnosticOriginal),
        //     diagnosticIntFirst,
        //     DoubleBitsAsPointer(diagnosticIntFirst),
        //     diagnosticIntSecond,
        //     DoubleBitsAsPointer(diagnosticIntSecond),
        //     diagnosticLevelFirst,
        //     DoubleBitsAsPointer(diagnosticLevelFirst),
        //     diagnosticLevelSecond,
        //     DoubleBitsAsPointer(diagnosticLevelSecond),
        //     diagnosticPer,
        //     DoubleBitsAsPointer(diagnosticPer),
        //     diagnosticDiff,
        //     DoubleBitsAsPointer(diagnosticDiff),
        //     charismaBonus)
        L2_LOG_ERROR(
            Log,
            u"[stats] Magic attack recheck for %s, object %u: base attack %.17g (bits %p), INT bonus read 1 %.17g (bits "
            u"%p), read 2 %.17g (bits %p), level bonus read 1 %.17g (bits %p), read 2 %.17g (bits %p), multiplier %.17g "
            u"(bits %p), additive bonus %.17g (bits %p); CHA multiplier used %.17g.",
            diagnosticName, m_nId, diagnosticOriginal, DoubleBitsAsPointer(diagnosticOriginal), diagnosticIntFirst,
            DoubleBitsAsPointer(diagnosticIntFirst), diagnosticIntSecond, DoubleBitsAsPointer(diagnosticIntSecond),
            diagnosticLevelFirst, DoubleBitsAsPointer(diagnosticLevelFirst), diagnosticLevelSecond,
            DoubleBitsAsPointer(diagnosticLevelSecond), diagnosticPer, DoubleBitsAsPointer(diagnosticPer), diagnosticDiff,
            DoubleBitsAsPointer(diagnosticDiff), charismaBonus
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
    }

    ValidateGradePenalty(VALIDATE::MATTACK);
    ClampMin(VALIDATE::MATTACK);
    return true;
}

bool CCreature::ValidatePDefend() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double levelBonus = LevelBonus();
    CSharedCreatureData* const data = m_data;
    const double candidate = ((static_cast<double>(data->m_nOrgParam[VALIDATE::PDEFEND]) * levelBonus) * charismaBonus) *
            m_SkillMod.m_Mods[CCreatureSkillMod::PDefend].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::PDefend].m_Diff;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::PDEFEND], candidate, 59);
    ValidateShieldDefend();
    ValidateShieldDefendRate();
    ValidateGradePenalty(VALIDATE::PDEFEND);
    ClampMin(VALIDATE::PDEFEND);
    return true;
}

bool CCreature::ValidateShieldDefend() {
    L2_GUARD_FUNCTION();
    CSharedCreatureData* const data = m_data;
    const double candidate = static_cast<double>(data->m_nOrgParam[VALIDATE::SHIELDDEFENSE]) *
            m_SkillMod.m_Mods[CCreatureSkillMod::Shield].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::Shield].m_Diff;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::SHIELDDEFENSE], candidate, 59);
    ClampMin(VALIDATE::SHIELDDEFENSE);
    return true;
}

bool CCreature::ValidateShieldDefendRate() {
    L2_GUARD_FUNCTION();
    const double constitutionBonus = ConBonusF();
    CSharedCreatureData* const data = m_data;
    const double candidate = constitutionBonus * static_cast<double>(data->m_nOrgParam[VALIDATE::SHIELDDEFENSERATE]) *
        m_SkillMod.m_dShieldDefenceModPer;
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dParam[VALIDATE::SHIELDDEFENSERATE], candidate, 59);
    ClampMin(VALIDATE::SHIELDDEFENSERATE);
    return true;
}

bool CCreature::ValidateMDefend() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double menBonus = MenBonusF();
    const int originalDefence = m_data->m_nOrgParam[VALIDATE::MDEFEND];
    const double levelBonus = LevelBonus();
    const double candidate = (((levelBonus * menBonus) * static_cast<double>(originalDefence)) * charismaBonus) *
            m_SkillMod.m_Mods[CCreatureSkillMod::MDefend].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::MDefend].m_Diff;
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[VALIDATE::MDEFEND], candidate, 59);
    if (IsPC() && -0.001 < candidate && candidate < 0.001) {
        const double diagnosticLevelBonus = LevelBonus();
        const double diagnosticMenBonus = MenBonusF();
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MDefend] illegal magic defense [%f] user[%s] "
        //     L"orgMDefend[%d] menBonusF[%f] levelBonus[%f] "
        //     L"skillMDefendPer[%f] skillMDefendDiff[%f]",
        //     candidate,
        //     name,
        //     originalDefence,
        //     diagnosticMenBonus,
        //     diagnosticLevelBonus,
        //     m_SkillMod.m_Mods[
        //         CCreatureSkillMod::MDefend].m_Per,
        //     m_SkillMod.m_Mods[
        //         CCreatureSkillMod::MDefend].m_Diff)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[stats] Near-zero magic defense %.17g calculated for player %s, object %u: base defense %d. Later
        //     observations: MEN bonus %.17g, level bonus %.17g, multiplier %.17g, additive bonus %.17g. Continuing
        //     grade-penalty and minimum-value checks.", candidate, name, m_nId, originalDefence, diagnosticMenBonus,
        //     diagnosticLevelBonus,
        //     m_SkillMod.m_Mods[
        //     CCreatureSkillMod::MDefend].m_Per,
        //     m_SkillMod.m_Mods[
        //     CCreatureSkillMod::MDefend].m_Diff)
        // L2_LOG_ERROR(Log,
        //     L"[stats] Near-zero magic defense %.17g calculated for player %s, object %u: base defense %d. Later
        //     observations: MEN bonus %.17g, level bonus %.17g, multiplier %.17g, additive bonus %.17g. Continuing
        //     grade-penalty and minimum-value checks.", candidate, name, m_nId, originalDefence, diagnosticMenBonus,
        //     diagnosticLevelBonus,
        //     m_SkillMod.m_Mods[
        //     CCreatureSkillMod::MDefend].m_Per,
        //     m_SkillMod.m_Mods[
        //     CCreatureSkillMod::MDefend].m_Diff)
        L2_LOG_ERROR(
            Log,
            u"[stats] Near-zero magic defense %.17g calculated for player %s, object %u: base defense %d; later "
            u"observations: MEN bonus %.17g, level bonus %.17g, multiplier %.17g, additive bonus %.17g. Continuing "
            u"grade-penalty and minimum-value checks.",
            candidate, name, m_nId, originalDefence, diagnosticMenBonus, diagnosticLevelBonus,
            m_SkillMod.m_Mods[CCreatureSkillMod::MDefend].m_Per, m_SkillMod.m_Mods[CCreatureSkillMod::MDefend].m_Diff
        );
    }
    ValidateGradePenalty(VALIDATE::MDEFEND);
    ClampMin(VALIDATE::MDEFEND);
    return true;
}

void CCreature::ValidateAttackSpeedAndTime() {
    L2_GUARD_FUNCTION();
    ValidateAttackSpeed();
    ValidateAttackTime();
}

bool CCreature::ValidateAttackSpeed() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double dexterityBonus = DexBonusF();
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        m_data->m_dParam[VALIDATE::PATTACKSPEED],
        static_cast<double>(m_data->m_nOrgParam[VALIDATE::PATTACKSPEED]) * charismaBonus * dexterityBonus *
                m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed].m_Per *
                m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeedByWeapon].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed].m_Diff +
            m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeedByWeapon].m_Diff,
        59
    );
    ValidateGradePenalty(VALIDATE::PATTACKSPEED);
    ClampMin(VALIDATE::PATTACKSPEED);
    return true;
}

bool CCreature::ValidatePUseSpeed() {
    L2_GUARD_FUNCTION();
    const double dexterityBonus = DexBonusF();
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        m_data->m_dParam[VALIDATE::PSKILLSPEED],
        dexterityBonus * (static_cast<double>(m_data->m_nOrgParam[VALIDATE::PATTACKSPEED]) / 333.0) *
                m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed].m_Per *
                m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeedByWeapon].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeed].m_Diff / 333.0 +
            m_SkillMod.m_Mods[CCreatureSkillMod::AttackSpeedByWeapon].m_Diff / 333.0,
        59
    );
    ValidateGradePenalty(VALIDATE::PSKILLSPEED);
    ClampMin(VALIDATE::PSKILLSPEED);
    return true;
}

bool CCreature::ValidateMUseSpeed() {
    L2_GUARD_FUNCTION();
    const double charismaBonus = std::sqrt(g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]));
    const double witBonus = WitBonusF();
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
        m_data->m_dParam[VALIDATE::MATTACKSPEED],
        witBonus * static_cast<double>(m_data->m_nOrgParam[VALIDATE::MATTACKSPEED]) * charismaBonus *
                m_SkillMod.m_Mods[CCreatureSkillMod::MagicSpeed].m_Per +
            m_SkillMod.m_Mods[CCreatureSkillMod::MagicSpeed].m_Diff / 333.0,
        59
    );
    ValidateGradePenalty(VALIDATE::MATTACKSPEED);
    ClampMin(VALIDATE::MATTACKSPEED);
    return true;
}

bool CCreature::ValidateAttackTime() {
    if (m_data->m_dParam[VALIDATE::PATTACKSPEED] == 0.0) {
        m_data->m_dAttackTime = 1.8;
    } else {
        m_data->m_dAttackTime = 500.0 / m_data->m_dParam[VALIDATE::PATTACKSPEED];
    }
    return true;
}

bool CCreature::ValidateAttackRange() {
    L2_GUARD_FUNCTION();
    const double attackRange =
        static_cast<double>(m_data->m_nAttackRange) * m_SkillMod.m_Mods[CCreatureSkillMod::Range].m_Per +
        m_SkillMod.m_Mods[CCreatureSkillMod::Range].m_Diff;
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        m_data->m_nAttackRange, runtime::Win64CvttDoubleToInt32(attackRange), 37
    );
    return true;
}

bool CCreature::ValidateAllOnWeaponArmors() {
    L2_GUARD_FUNCTION();
    ValidateWeaponType();
    ValidateArmorType();
    return true;
}

void CCreature::ValidateAllOnSIM() {
    L2_GUARD_FUNCTION();

    ValidateCarryWeight();
    ValidateStorageSlot();
    ValidateOrgPAttack();
    ValidateOrgMAttack();
    ValidateOrgPDefend();
    ValidateOrgMDefend();
    ValidateOrgMUseSpeed();
    ValidateOrgAttackSpeed();
    ValidatePUseSpeed();
    ValidateOrgPCriticalProb();
    ValidateOrgMCriticalProb();
    ValidateOrgPHit();
    ValidateOrgMHit();
    ValidateOrgPAvoidRate();
    ValidateOrgMAvoidRate();
    ValidateAttackRange();
    ValidateExtraParam();

    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        npc->ParamChanged(NPCT_VALIDATE);
    } else if (IsPC()) {
        ParamChanged(PCB_USER);
    } else {
        ParamChanged();
    }

    UpdateSummonStatBonus();
}

void CCreature::UpdateSummonStatBonus(CSummon* summon, CCreatureSkillMod::SummonStatBonusMod mod) {
    L2_GUARD_FUNCTION();
    if (summon == nullptr) {
        return;
    }

    User* const master = summon->GetMaster();
    if (master == nullptr) {
        return;
    }

    if (0.0 < master->m_SkillMod.m_ModSummonBonus[mod]) {
        switch (mod) {
            case CCreatureSkillMod::SBMN_PAttack:
                summon->ValidatePAttack();
                break;
            case CCreatureSkillMod::SBMN_PDefend:
                summon->ValidatePDefend();
                break;
            case CCreatureSkillMod::SBMN_MAttack:
                summon->ValidateMAttack();
                break;
            case CCreatureSkillMod::SBMN_MDefend:
                summon->ValidateMDefend();
                break;
            case CCreatureSkillMod::SBMN_HP:
                summon->ValidateBaseHP();
                break;
            case CCreatureSkillMod::SBMN_MP:
                summon->ValidateBaseMP();
                break;
            case CCreatureSkillMod::SBMN_PAttackSpeed:
                summon->ValidateAttackSpeedAndTime();
                summon->ValidatePUseSpeed();
                break;
            case CCreatureSkillMod::SBMN_MUseSpeed:
                summon->ValidateMUseSpeed();
                break;
            case CCreatureSkillMod::SBMN_Speed:
                summon->ValidateMoveToTargetSpeedAndSendInfo();
                break;
            case CCreatureSkillMod::SBMN_CriticalProb:
                summon->ValidatePCriticalProb();
                break;
            default:
                break;
        }
        m_OnSummonStatBonus = true;
    }
    summon->ValidateSummonAttackSpeed();
}

void CCreature::UpdateSummonStatBonus() {
    L2_GUARD_FUNCTION();

    User* user = nullptr;
    if (IsPC()) {
        user = CastUser();
    } else if (IsSummon()) {
        user = GetUserOrMaster();
    }
    if (user == nullptr) {
        return;
    }

    FOR_ALL_SUMMON_IN_SUMMON_LIST(
        user, summon, UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_PAttack);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_MAttack);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_PDefend);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_MDefend);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_HP); UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_MP);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_PAttackSpeed);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_MUseSpeed);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_Speed);
        UpdateSummonStatBonus(summon, CCreatureSkillMod::SBMN_CriticalProb);
        if (m_OnSummonStatBonus) { summon->SendSummonInfo(true, false); }
    );
    if (m_OnSummonStatBonus) {
        m_OnSummonStatBonus = false;
    }
}

bool CCreature::SetAllOverPointToDefault() {
    const bool hpOver = m_data->m_dBaseHP <= m_data->m_dHP && m_data->m_dHP != m_data->m_dBaseHP;
    if (hpOver) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 11);
    }

    const bool mpOver = m_data->m_dBaseMP <= m_data->m_dMP && m_data->m_dMP != m_data->m_dBaseMP;
    if (mpOver) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 12);
    }

    const bool cpOver = m_data->m_dBaseCP <= m_data->m_dCP && m_data->m_dCP != m_data->m_dBaseCP;
    if (cpOver) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCP, m_data->m_dBaseCP, 13);
    }

    return hpOver || mpOver || cpOver;
}

bool CCreature::OnRegenTimer(FVector& position) {
    // Current L2Server 0x00605b2c.
    L2_GUARD_FUNCTION();

    if (m_data->m_bIsFreezed != 0 || (IsPC() && CastUser()->m_bIgnoreRegeneration)) {
        return false;
    }

    if (IsPC()) {
        User* const user = CastUser();
        if (user->m_DuelUserInfo.IsDuelDead(user)) {
            return false;
        }
    }
    if (IsPC()) {
        User* const user = CastUser();
        if (user->m_PledgeGameUser.IsPlayer() && !user->m_PledgeGameUser.IsAlive()) {
            return false;
        }
    }

    const MoveModeEnum moveMode = GetMoveMode();
    const std::size_t mode = static_cast<std::size_t>(moveMode);
    double hpRegen = HPRegenBonusByMotionEx() * m_data->m_dHPRegen * m_SkillMod.m_ModsMove[0][mode].m_Per +
        m_SkillMod.m_ModsMove[0][mode].m_Diff;
    double mpRegen = MPRegenBonusByMotionEx() * m_data->m_dMPRegen * m_SkillMod.m_ModsMove[1][mode].m_Per +
        m_SkillMod.m_ModsMove[1][mode].m_Diff;
    double cpRegen = CPRegenBonusByMotionEx() * m_data->m_dCPRegen * m_SkillMod.m_ModsMove[2][mode].m_Per +
        m_SkillMod.m_ModsMove[2][mode].m_Diff;

    double battleCampBonus = 1.0;
    if (IsPC()) {
        User* const user = CastUser();
        const int battleCampType = user->GetAffectedBattleCampType();
        if (battleCampType == 1) {
            battleCampBonus = 1.3;
        } else if (battleCampType == 2) {
            battleCampBonus = 2.0;
        }
    }

    double hpResidenceBonus = 1.0;
    double mpResidenceBonus = 1.0;
    if (IsPC()) {
        User* const user = CastUser();
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr && pledge->HasResidence() && IsInRealWorld()) {
            CResidence* residence = nullptr;
            if (CCastle* const castle = pledge->GetCastle(); castle != nullptr && castle->IsInResidence(position, false)) {
                residence = castle;
            } else if (CWorldAgit* const agit = pledge->GetWorldAgit();
                agit != nullptr && agit->IsInResidence(position, false)) {
                residence = agit;
            } else if (CFortress* const fortress = pledge->GetFortress();
                fortress != nullptr && fortress->IsInResidence(position, false)) {
                residence = fortress;
            }

            if (residence != nullptr) {
                if ((residence->m_nDecoFunc & 0x02U) != 0U) {
                    hpResidenceBonus = residence->m_AgitDecoEffect.dHpBonus;
                }
                if ((residence->m_nDecoFunc & 0x04U) != 0U) {
                    mpResidenceBonus = residence->m_AgitDecoEffect.dMpBonus;
                }
            }
        }

        if (!IsInRealWorld()) {
            CInstantAgit* const agit = CInstantAgitManager::inst().GetUsingInstantAgit(*CastUser());
            if (agit != nullptr) {
                hpResidenceBonus = agit->GetHpBonus();
                mpResidenceBonus = agit->GetMpBonus();
            }
        }
    }

    bool changed = false;
    if (m_data->m_dHP <= m_data->m_dBaseHP && m_data->m_dBaseHP != m_data->m_dHP) {
        hpRegen = (hpRegen + m_AreaMod.m_dHpRegenAreaBonus) * hpResidenceBonus * battleCampBonus;
        if (hpRegen <= 1.0) {
            hpRegen = 1.0;
        }
        IncreaseHP_diff(hpRegen, true, 1, 0, true);
        changed = true;
    }

    if (IsPC() && m_data->m_dCP <= m_data->m_dBaseCP && m_data->m_dBaseCP != m_data->m_dCP) {
        if (cpRegen <= 1.0) {
            cpRegen = 1.0;
        }
        IncreaseCP_diff(cpRegen, 2, 0, true);
        CastUser()->BroadcastCP(0, 0);
        changed = true;
    }

    if (m_data->m_dMP <= m_data->m_dBaseMP && m_data->m_dBaseMP != m_data->m_dMP) {
        mpRegen = (mpRegen + m_AreaMod.m_dMpRegenAreaBonus) * mpResidenceBonus * battleCampBonus;
        if (mpRegen <= 1.0) {
            mpRegen = 1.0;
        }
        IncreaseMP_diff(mpRegen, 3, 0, true);
        if (m_data->m_bIsAlive == 0) {
            m_data->SetHP(0.0);
        }
        changed = true;
    }

    if (changed || SetAllOverPointToDefault()) {
        if (IsSummonOrPet()) {
            CastSummon()->SendSummonStatusToMaster(0);
        }

        if (!g_bSendSummonHPMPInfo && IsSummon()) {
            g_ptlUserMap->BroadcastToNeighborParty(
                this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId), 0, 1, 1, 9,
                runtime::Win64CvttDoubleToInt32(m_data->m_dHP)
            );
        } else {
            g_ptlUserMap->BroadcastToTargettingNeighbor(
                this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId), 0, 1, 1, 9,
                runtime::Win64CvttDoubleToInt32(m_data->m_dHP)
            );
        }
        m_data->m_NeedUpdatePacketBitSet.ClearBit(0xb);
    }
    return true;
}

void CCreature::ValidateHPMPCP(bool preserveOverPoint) {
    L2_GUARD_FUNCTION();
    ValidateOrgHP();
    ValidateOrgMP();
    ValidateOrgCP();
    ValidateOrgHPRegen();
    ValidateOrgMPRegen();
    ValidateOrgCPRegen();
    if (IsPet()) {
        CastPet()->ValidateMeal();
    }
    if (!preserveOverPoint) {
        SetAllOverPointToDefault();
    }
}

int CCreature::LevelUp(unsigned int levelCount) {
    // Current L2Server 0x006194c0.
    L2_GUARD_FUNCTION();
    if (levelCount == 0) {
        return m_data->m_nLevel;
    }

    const int oldLevel = m_data->m_nLevel;
    const std::uint32_t newLevelBits = static_cast<std::uint32_t>(oldLevel) + levelCount;
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nLevel, static_cast<std::int32_t>(newLevelBits), 5);

    const int newLevel = m_data->m_nLevel;
    if (IsPC()) {
        switch (newLevel) {
            case 4:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_4);
                break;
            case 5:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_5);
                break;
            case 6:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_6);
                break;
            case 7:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_7);
                break;
            case 9:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_9);
                break;
            case 10:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_10);
                break;
            case 12:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_12);
                break;
            case 15:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_15);
                break;
            case 18:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_20);
                break;
            case 35:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_35);
                break;
            case 36:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_36);
                break;
            case 39:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_39);
                break;
            case 61:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_61);
                break;
            case 73:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_73);
                break;
            case 75:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_75);
                break;
            case 76:
                CastUser()->CheckTutorialEvent(TCE_LEVEL_76);
                break;
            default:
                break;
        }

        User* const user = CastUser();
        m_data->m_SubJobLevel[m_data->m_nSubJobId] = newLevel;
        if (static_cast<unsigned int>(newLevel - 70) < 6U && m_data->m_nPledgeID != 0 &&
            m_data->m_nAllianceId == UINT32_MAX) {
            runtime::raw_atomic::ExchangeI32(&user->m_nRelationChanged, 2);
        }

        int missionLevel = newLevel;
        if (m_data->m_nSubJobId == 0) {
            while (oldLevel < missionLevel) {
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PC_MAIN_LEVEL_UP, missionLevel, 1);
                --missionLevel;
            }
        } else if (m_data->m_IsDualClass) {
            while (oldLevel < missionLevel) {
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_PC_DUAL_LEVEL_UP, missionLevel, 1);
                --missionLevel;
            }
        }
    }

    AcquireSkills(g_SkillAcquireDB.m_vectorClsSkill[static_cast<std::size_t>(m_data->m_nClass)], true, false);
    AcquireSkills(g_SkillAcquireDB.m_vectorRaceSkill[static_cast<std::size_t>(m_data->m_Race)], true, false);

    if (IsPC()) {
        CastUser()->OnRuneItemsChange();
    }
    SocialAction(0x84a, 0x800, false);

    if (IsPC()) {
        User* const user = CastUser();
        user->SendSystemMessage(SystemMessageId::LEVEL_INCREASED);
        if (CPledge* const pledge = user->GetPledge()) {
            pledge->AddDbMember(user);
            pledge->ValidateTotalLevel();
            pledge->Online(user);
        }
        user->SendGIPCharacterInfo(true);
        user->SendLevelUpNPCEvent();
        user->SendVoteSystemInfoPacket();
        user->SendNewSkillByLevelUp();
        user->UpdateMentorByLevelUp(false, oldLevel);
        user->SendPathToAwakeningLevelNotice();
    }
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM);
    } else {
        ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM);
    }

    SetMaxMagicLevel();
    OnLevelChanged(static_cast<int>(levelCount));

    const FVector position = GetPos();
    bool regenerateOnLevelUp = true;
    if (m_AreaMod.m_bIsInSSQZone) {
        CArea* const area = CAreaDB::inst().FindArea(static_cast<int>(GetInZoneID()), position, AREAT_SSQ, true);
        regenerateOnLevelUp = area == nullptr || area->HPMPRegenOnLevelUp();
    }
    if (regenerateOnLevelUp) {
        ValidateChanged(VB_LEVELUP);
    }

    if (IsPC()) {
        User* const user = CastUser();
        DynamicQuestManager::Inst()->OnUserEnterWorld(*user);
        CEventCampaignManager::Inst()->OnUserEnterWorld(*user);

        if (g_bDualBillingSystem && newLevel == g_nDualBillingSystemLevel) {
            authd.AuthFreeToPayUphill(static_cast<int>(user->GetDBID()), static_cast<int>(user->GetAccountID()), newLevel);
            if (user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
                user->GetSocket()->Send("chddd", 0xfe, 0xfe, 0, 1, 0);
            }
        }
    }
    return newLevel;
}

int CCreature::LevelDown(unsigned int levelCount) {
    // Current L2Server 0x0061a9e4.
    L2_GUARD_FUNCTION();
    if (levelCount == 0) {
        return m_data->m_nLevel;
    }

    const std::uint32_t newLevelBits = static_cast<std::uint32_t>(m_data->m_nLevel) - levelCount;
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nLevel, static_cast<std::int32_t>(newLevelBits), 5);

    if (IsPC() && m_data->m_nLevel < 70 && m_data->m_nPledgeID != 0 && m_data->m_nAllianceId == UINT32_MAX) {
        User* const user = CastUser();
        L2_GUARD(*user->m_lock);
        user->m_nRelationChanged = 2;
    }

    const int newLevel = m_data->m_nLevel;
    if (IsPC()) {
        User* const user = CastUser();
        m_data->m_SubJobLevel[m_data->m_nSubJobId] = newLevel;
        if (static_cast<unsigned int>(newLevel - 70) < 6 && m_data->m_nPledgeID != 0 &&
            m_data->m_nAllianceId == UINT32_MAX) {
            L2_GUARD(*user->m_lock);
            user->m_nRelationChanged = 2;
        }
    }

    const auto& classSkills = g_SkillAcquireDB.m_vectorClsSkill[static_cast<std::size_t>(m_data->m_nClass)];
    for (SkillAcquireInfo* const acquireInfo : classSkills) {
        if (acquireInfo == nullptr) {
            continue;
        }

        if (acquireInfo->bAutoGet && newLevel < acquireInfo->nGetLevel &&
            HaveSkill(acquireInfo->nSkillID, acquireInfo->nSkillLevel)) {
            DeleteAcquireSkill(acquireInfo->nSkillID);
        }

        if (!IsPC() || newLevel + 3 >= acquireInfo->nGetLevel) {
            continue;
        }

        const int acquiredLevel = GetAcquiredSkillLevel(acquireInfo->nSkillID);
        int acquiredSubLevel = GetAcquiredSkillSubLevel(acquireInfo->nSkillID);
        if (acquiredLevel == -1 || acquiredLevel < acquireInfo->nSkillLevel || acquiredSubLevel < 0) {
            continue;
        }

        if (acquireInfo->nSkillLevel == 1) {
            DeleteAcquireSkill(acquireInfo->nSkillID);
            CastUser()->SendDeletedSkillToDB(acquireInfo->nSkillID);
            continue;
        }

        if (!HaveSkill(acquireInfo->nSkillID, acquireInfo->nSkillLevel)) {
            continue;
        }

        const int destinationLevel = acquireInfo->nSkillLevel - 1;
        if (g_SkillDB.GetSkillInfo(CSkillKey(acquireInfo->nSkillID, destinationLevel, acquiredSubLevel), nullptr) ==
            nullptr) {
            acquiredSubLevel = 0;
        }
        if (AcquireSkill(acquireInfo->nSkillID, destinationLevel, acquiredSubLevel, true, true)) {
            CastUser()->SendAcquiredSkillToDB(acquireInfo->nSkillID, destinationLevel, acquiredSubLevel, true);
        }
    }

    AcquireSkills(classSkills, true, false);
    AcquireSkills(g_SkillAcquireDB.m_vectorRaceSkill[static_cast<std::size_t>(m_data->m_Race)], true, false);

    if (IsPC()) {
        CastUser()->ValidateChanged(std::int64_t{VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_SIM});
    } else {
        ValidateChanged(std::int64_t{VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_SIM});
    }

    if (m_data->m_dBaseHP <= m_data->m_dHP && m_data->m_dHP != m_data->m_dBaseHP) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 11);
    }
    if (m_data->m_dBaseMP <= m_data->m_dMP && m_data->m_dMP != m_data->m_dBaseMP) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 12);
    }

    if (IsPC()) {
        if (m_data->m_dBaseCP <= m_data->m_dCP && m_data->m_dCP != m_data->m_dBaseCP) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCP, m_data->m_dBaseCP, 13);
        }

        User* const user = CastUser();
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr) {
            pledge->AddDbMember(user);
            pledge->ValidateTotalLevel();
            pledge->Online(user);
        }
        user->OnRuneItemsChange();
        user->ValidateSkillList();
        user->SendGIPCharacterInfo(true);
        user->SendVoteSystemInfoPacket();
    }

    SetMaxMagicLevel();
    OnLevelChanged(-static_cast<int>(levelCount));
    return m_data->m_nLevel;
}

bool CCreature::ValidateLevel() {
    // Current L2Server 0x0061cc28.
    L2_GUARD_FUNCTION();

    const std::int64_t experience = m_data->m_nExp;
    int boundedLevel = m_data->m_nLevel;
    const bool isPC = IsPC();
    int maximumLevel = isPC ? 120 : 121;

    if (isPC) {
        User* const user = CastUser();
        if (g_nCountryCode == 1 && user != nullptr && user->GetSocket() != nullptr &&
            user->GetSocket()->m_bEconomicRestriction) {
            maximumLevel = 40;
        }
        if (m_data->m_nSubJobId > 0 && !m_data->m_IsDualClass) {
            maximumLevel = 80;
        }
    }

    if (boundedLevel < 1) {
        boundedLevel = 1;
    } else if (boundedLevel > maximumLevel) {
        boundedLevel = maximumLevel;
    }

    if (experience < ExpByLevel[boundedLevel - 1]) {
        if (boundedLevel < 2) {
            return false;
        }
        int destinationLevel = boundedLevel - 1;
        while (destinationLevel >= 0 && experience < ExpByLevel[destinationLevel]) {
            --destinationLevel;
        }
        LevelDown(static_cast<unsigned int>(boundedLevel - destinationLevel - 1));
    }

    if (ExpByLevel[boundedLevel] <= experience) {
        int destinationLevel = boundedLevel;
        int experienceIndex = boundedLevel;
        while (experienceIndex < maximumLevel && ExpByLevel[experienceIndex] <= experience) {
            ++destinationLevel;
            ++experienceIndex;
        }
        LevelUp(static_cast<unsigned int>(destinationLevel - boundedLevel));
    }

    if (boundedLevel > maximumLevel) {
        boundedLevel = maximumLevel;
    }
    if (boundedLevel == m_data->m_nLevel) {
        return false;
    }

    if (IsSummonOrPet()) {
        CSummon* const summon = CastSummon();
        summon->SendSummonStatusToMaster(0);
        return false;
    }
    if (!IsPC()) {
        return false;
    }

    db.SendSaveCharacterInfo(CastUser(), false, true);
    if (boundedLevel < m_data->m_nLevel) {
        db.RequestNewbieCheck(CastUser());
    }
    return true;
}

bool CCreature::ValidateLevelUP() {
    L2_GUARD_FUNCTION();
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 11);
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 12);
    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCP, m_data->m_dBaseCP, 13);
    return false;
}

bool CCreature::ValidateMoveToTargetSpeedAndSendInfo() { return false; }

void CCreature::SetMorphID(int classId) {
    L2_GUARD_FUNCTION();
    m_SkillDurationMod.m_nPolymorphID = classId;
}

int CCreature::GetMorphID() { return m_SkillDurationMod.m_nPolymorphID; }

bool CCreature::ValidateCarryWeight() {
    // Current L2Server 0x005f36a4.
    L2_GUARD_FUNCTION();

    int inventoryWeight = m_inv.GetCarryingWeight();
    m_data->m_nInventoryWeight = inventoryWeight;

    const double carryWeight = GetWeightLimit(IsPC() ? 69000.0 : 34500.0);
    if (m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCarryWeight, carryWeight, 0x0f) && IsPC()) {
        ParamChanged(std::int64_t{PCB_USER});
    }

    if (inventoryWeight < 0) {
        inventoryWeight = runtime::Win64CvttDoubleToInt32(m_data->m_dCarryWeight);
    }

    const int previousWeightLevel = m_nCurrentWeight;
    const auto publishWeightPenalty = [this](const int level, const double speed, const double regeneration) {
        m_nCurrentWeight = level;
        m_dSpeedModByWeight = speed;
        m_dHPRegenModByWeight = regeneration;
        m_dMPRegenModByWeight = regeneration;
        m_dCPRegenModByWeight = regeneration;
    };

    if (m_bDietMode ||
        inventoryWeight < runtime::Win64CvttDoubleToInt32(GetWeightPenalty(m_data->m_dCarryWeight * 0.5))) {
        // publishWeightPenalty(0, 1.0, 1.0)
        publishWeightPenalty(WEIGHT_NORMAL, 1.0, 1.0);
    } else if (inventoryWeight < runtime::Win64CvttDoubleToInt32(GetWeightPenalty(m_data->m_dCarryWeight * 0.666))) {
        // publishWeightPenalty(1, 1.0, 0.5)
        publishWeightPenalty(WEIGHT_LIGHT, 1.0, 0.5);
    } else if (inventoryWeight < runtime::Win64CvttDoubleToInt32(GetWeightPenalty(m_data->m_dCarryWeight * 0.8))) {
        // publishWeightPenalty(2, 0.5, 0.5)
        publishWeightPenalty(WEIGHT_HEAVY, 0.5, 0.5);
    } else if (inventoryWeight < runtime::Win64CvttDoubleToInt32(GetWeightPenalty(m_data->m_dCarryWeight))) {
        // publishWeightPenalty(3, 0.5, 0.5)
        publishWeightPenalty(WEIGHT_OVERWEIGHT, 0.5, 0.5);
    } else {
        // publishWeightPenalty(4, 0.0, 0.1)
        publishWeightPenalty(WEIGHT_SUPEROVER, 0.0, 0.1);
    }

    ValidateSpeed();
    ValidateHPRegen();
    ValidateMPRegen();

    if (IsPC() && previousWeightLevel != m_nCurrentWeight) {
        ParamChanged(std::int64_t{PCB_CHAR_CARRYWEIGHT});
        CastUser()->SendETCBuffStatusInfo();
    }

    if (IsPC()) {
        if (CastUser()->GetPrivateStoreType() == PST_BUY_OPEN && CastUser()->CheckPrivateStoreInventory()) {
            CastUser()->QuitPrivateStore(false);
        }
    }
    return true;
}

double CCreature::GetWeightPenalty(double weightPenalty) {
    L2_GUARD_FUNCTION();
    return m_SkillMod.ApplyBonus(weightPenalty, CCreatureSkillMod::WeightPenalty);
}

double CCreature::GetWeightLimit(double weightLimit) {
    L2_GUARD_FUNCTION();
    return m_SkillMod.ApplyBonus(ConBonusF() * weightLimit, CCreatureSkillMod::WeightLimit);
}

double CCreature::GetWeaponRandomDamage() const {
    L2_GUARD_FUNCTION();
    if (m_bIgnoreWeaponRandomDamage) {
        return 1.0;
    }

    const double randomDamage = static_cast<double>(m_data->m_nRandomDamage);
    const double negativeRandomDamage = -randomDamage;
    return (::genrand64_real1() * (randomDamage - negativeRandomDamage) + negativeRandomDamage) * 0.01 + 1.0;
}

double CCreature::GetSoulShotExtraBonus() {
    // Current L2Server 0x005eadac.
    return m_dExtraParam[0];
}

double CCreature::GetWeaponTraitBonus(::AttackType attackType) {
    L2_GUARD_FUNCTION();

    double defendingTrait = 1.0;
    switch (attackType) {
        case ::AT_SWORD:
        case ::AT_TWOHANDSWORD:
        case ::AT_BUSTER:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_SWORD];
            break;
        case ::AT_BLUNT:
        case ::AT_TWOHANDBLUNT:
        case ::AT_STAFF:
        case ::AT_TWOHANDSTAFF:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_BLUNT];
            break;
        case ::AT_DAGGER:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_DAGGER];
            break;
        case ::AT_POLE:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_POLE];
            break;
        case ::AT_FIST:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_FIST];
            break;
        case ::AT_BOW:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_BOW];
            break;
        case ::AT_ETC:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_AT_ETC];
            break;
        case ::AT_DUAL:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_DUAL];
            break;
        case ::AT_DUALFIST:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_DUALFIST];
            break;
        case ::AT_RAPIER:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_RAPIER];
            break;
        case ::AT_CROSSBOW:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_CROSSBOW];
            break;
        case ::AT_ANCIENTSWORD:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_ANCIENTSWORD];
            break;
        case ::AT_DUALDAGGER:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_DUALDAGGER];
            break;
        case ::AT_TWOHANDCROSSBOW:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_TWOHANDCROSSBOW];
            break;
        case ::AT_DUALBLUNT:
            defendingTrait = m_dzDefendingTraitValue[TRAIT_DUALBLUNT];
            break;
        default:
            break;
    }
    return 1.0 - (defendingTrait - 1.0);
}

double CCreature::GetGeneralTraitBonus(CCreature& creature, TraitTypeEnum traitType, bool weaponTrait) {
    L2_GUARD_FUNCTION();
    const std::uint32_t traitIndex = static_cast<std::uint32_t>(traitType);
    if (traitIndex - 1U > 64U) {
        return 1.0;
    }
    if (creature.m_bzDefendingTraitPerfect[traitIndex]) {
        return 0.0;
    }

    if (g_TraitCateroty[traitIndex] == TRAIT_CATEGORY_RACE) {
        if (!m_bzAttackingTrait[traitIndex] || !creature.m_bzDefendingTrait[traitIndex]) {
            return 1.0;
        }
    } else if (g_TraitCateroty[traitIndex] != TRAIT_CATEGORY_STATUS || weaponTrait) {
        return 1.0;
    }

    const double bonus = m_dzAttackingTraitValue[traitIndex] - creature.m_dzDefendingTraitValue[traitIndex] + 1.0;
    if (bonus < 0.05) {
        return 0.05;
    }
    if (2.0 < bonus) {
        return 2.0;
    }
    return bonus;
}

std::int64_t CCreature::GetInt64Param(SetL2ParamEnum parameter) {
    switch (parameter) {
        case STE_LEVEL:
            return m_data->m_nLevel;
        case STE_STR:
            return m_data->m_Stat.m_N[1];
        case STE_DEX:
            return m_data->m_Stat.m_N[4];
        case STE_CON:
            return m_data->m_Stat.m_N[2];
        case STE_INT:
            return m_data->m_Stat.m_N[0];
        case STE_MEN:
            return m_data->m_Stat.m_N[3];
        case STE_WIT:
            return m_data->m_Stat.m_N[5];
        case STE_EXP:
            return m_data->m_nExp;
        case STE_P_ABNORMAL_RESIST:
            return m_data->m_AbnormalResist.m_N[0];
        case STE_M_ABNORMAL_RESIST:
            return m_data->m_AbnormalResist.m_N[1];
        case STE_LUC:
            return m_data->m_Stat.m_N[6];
        case STE_CHA:
            return m_data->m_Stat.m_N[7];
        default:
            return 0;
    }
}

void CCreature::GetAttackTypeValue(const CSkillInfo* skillInfo, int& attackType, int& attackValue) {
    L2_GUARD_FUNCTION();

    attackType = m_data->m_BaseAttribute.m_nAttackType;
    attackValue = m_data->m_BaseAttribute.m_nAttackValue;

    const int summonerCategory = static_cast<int>(g_PrecompiledHeaderDB.Search(u"attribute_group_summoner"));
    if (summonerCategory == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d cannot find "
        //     L"attribute_group_summoner category",
        //     kCreatureServerSourceFile,
        //     0x4ef2)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attributes] Attack-attribute lookup for object %u cannot find category attribute_group_summoner. Continuing
        //     with category ID -1.", m_nId)
        L2_LOG_ERROR(
            Log,
            "[attributes] Attack-attribute lookup for object %u cannot find category attribute_group_summoner; continuing "
            "with category ID -1.",
            m_nId
        );
    }

    bool handledPlayerSummoner = false;
    if (IsPC()) {
        User* const user = CastUser();
        if (user->HaveSummon()) {
            if (CCategoryDataDB::Inst().IsInCategory(m_data->m_nClass, summonerCategory)) {
                static_cast<void>(IsInItemGradePenalty());
            }
            handledPlayerSummoner = true;
        }
    }

    if (!handledPlayerSummoner && m_data->IsSummon()) {
        CSummon* const summon = CastSummon();
        User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
        if (master != nullptr && CCategoryDataDB::Inst().IsInCategory(master->m_data->m_nClass, summonerCategory) &&
            !master->IsInItemGradePenalty()) {
            attackType = master->m_data->m_BaseAttribute.m_nAttackType;
            attackValue = master->m_data->m_BaseAttribute.m_nAttackValue;
        }
    }

    if (skillInfo == nullptr) {
        if (attackType == ATTRT_NONE) {
            attackValue = 0;
        }
        return;
    }

    if (skillInfo->m_eAttribute != ATTRT_NONE) {
        if (attackType == skillInfo->m_eAttribute) {
            attackValue += skillInfo->m_AttributePoint;
        } else {
            attackType = skillInfo->m_eAttribute;
            attackValue = skillInfo->m_AttributePoint;
        }
    }
}

void CCreature::GetDefendValue(CCreature* creature, int attackType, int& defendValue) {
    L2_GUARD_FUNCTION();

    defendValue = 0;
    if (creature == nullptr) {
        return;
    }

    CSharedCreatureData* data = creature->m_data;
    if (data->IsSummon()) {
        const int summonerCategory = static_cast<int>(g_PrecompiledHeaderDB.Search(u"attribute_group_summoner"));
        User* const master = creature->GetUserOrMaster();
        if (master == nullptr || !CCategoryDataDB::Inst().IsInCategory(master->m_data->m_nClass, summonerCategory) ||
            master->IsInItemGradePenalty()) {
            return;
        }
        data = master->m_data;
    }

    short defendValues[ATTRT_MAX]{};
    std::copy_n(data->m_BaseAttribute.m_nDefendValue, ATTRT_MAX, defendValues);

    if (creature->IsPC() && IsNPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr) {
            short* const bonus = user->m_SkillMod.GetPVEAttrDefenceBonus(IsBoss() ? 2 : 1);
            if (bonus != nullptr) {
                for (int index = 0; index < ATTRT_MAX; ++index) {
                    defendValues[index] = static_cast<short>(defendValues[index] + bonus[index]);
                }
            }
        }
    }

    if (attackType == ATTRT_NONE) {
        for (const short value : defendValues) {
            defendValue = std::max(defendValue, static_cast<int>(value));
        }
    } else if (static_cast<unsigned int>(attackType) < ATTRT_MAX) {
        defendValue = defendValues[attackType];
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CCreature[%s]::GetAttributeBonus() - "
        //     L"Invalid AttackType[%d]!",
        //     GetName(),
        //     attackType)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[attributes] Defense lookup requested by %s, object %u, received invalid attack attribute %d for defender
        //     object %u, returning defense 0.", GetName(), m_nId, attackType, creature->m_nId)
        // L2_LOG_ERROR(Log,
        //     L"[attributes] Defense lookup requested by %s, object %u, received invalid attack attribute %d for defender
        //     object %u, returning defense 0.", GetName(), m_nId, attackType, creature->m_nId)
        L2_LOG_ERROR(
            Log,
            u"[attributes] Defense lookup requested by %s, object %u, received invalid attack attribute %d for defender "
            u"object %u; returning defense 0.",
            GetName(), m_nId, attackType, creature->m_nId
        );
    }

    defendValue = std::max(defendValue, -99);
}

double CCreature::GetAttributeBonus(CObject* target, const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return 1.0;
    }

    int attackType = ATTRT_NONE;
    int attackValue = 0;
    GetAttackTypeValue(skillInfo, attackType, attackValue);

    int defendValue = 0;
    GetDefendValue(target->CastCreature(), attackType, defendValue);

    const int difference = attackValue - defendValue;
    double upperBoundary = 0.0;
    double lowerBoundary = 0.0;
    GetBonusBoundary(difference, upperBoundary, lowerBoundary);

    double bonus = 0.0;
    if (difference != 0) {
        const int sign = difference < 0 ? -1 : 1;
        const int absolute = std::abs(difference);
        bonus = (std::pow(static_cast<double>(absolute / 2), 0.5) * static_cast<double>(absolute) * 0.01 + 2.5) *
            static_cast<double>(sign);
    }

    bonus = std::max(lowerBoundary, bonus);
    bonus = std::min(upperBoundary, bonus);
    return bonus / 100.0 + 1.0;
}

double CCreature::GetSkillAttributeBonus(CCreature* target, const CSkillInfo* skillInfo, bool weaponTrait) {
    L2_GUARD_FUNCTION();
    return GetGeneralTraitBonus(*target, skillInfo->m_TraitType, weaponTrait) * GetAttributeBonus(target, skillInfo);
}

double CCreature::GetSpritShotExtraBonus() const {
    // Current L2Server 0x005eadb8.
    return m_dExtraParam[1];
}

double CCreature::GetAttackTraitBonus(CCreature& creature) {
    L2_GUARD_FUNCTION();
    const double weaponTraitBonus = creature.GetWeaponTraitBonus(m_data->m_AttackType);
    if (weaponTraitBonus == 0.0) {
        return 0.0;
    }

    double traitBonus = 1.0;
    for (int trait = TRAIT_POISON; trait < TRAIT_MAX; ++trait) {
        if (g_TraitCateroty[trait] == TRAIT_CATEGORY_RACE && m_bzAttackingTrait[trait] &&
            creature.m_bzDefendingTrait[trait]) {
            traitBonus *= GetGeneralTraitBonus(creature, static_cast<TraitTypeEnum>(trait), true);
        }
    }
    if (traitBonus == 0.0) {
        return 0.0;
    }

    const double bonus = traitBonus * weaponTraitBonus;
    if (bonus < 0.05) {
        return 0.05;
    }
    if (2.0 < bonus) {
        return 2.0;
    }
    return bonus;
}

double CCreature::GetResurrectExpRatio(double resurrectExpRatio, double bonus) {
    L2_GUARD_FUNCTION();
    const double baseRatio = resurrectExpRatio / 100.0;
    const double bonusRatio = (resurrectExpRatio * bonus) / 100.0;
    const double maximumRatio = (resurrectExpRatio + 20.0) / 100.0;

    const double lowerBoundedRatio = bonusRatio > baseRatio ? bonusRatio : baseRatio;
    return maximumRatio > lowerBoundedRatio ? lowerBoundedRatio : maximumRatio;
}

void CCreature::GetBonusBoundary(int, double& upperBoundary, double& lowerBoundary) {
    L2_GUARD_FUNCTION();
    upperBoundary = 25.0;
    lowerBoundary = -25.0;
}

double CCreature::GetDamageByLimit(double damage) {
    const double limit = m_SkillMod.m_dDamageLimit;
    const int sign = 0.0 < damage ? 1 : -1;
    if (0.0 < limit && limit < static_cast<double>(sign) * damage) {
        return static_cast<double>(sign) * limit;
    }
    return damage;
}

double CCreature::HeightDamage(double height) {
    L2_GUARD_FUNCTION();
    if (0.0 < height) {
        const int safeHeight = g_PCParamDB.GetIntValueByField(
            m_data->m_Sex, static_cast<ClassType>(static_cast<std::int32_t>(m_data->m_nClass)), PC_SAFE_FALL_HEIGHT,
            m_data->m_Race
        );
        if (static_cast<double>(safeHeight) < height) {
            double effectiveHeight =
                height - static_cast<double>(safeHeight) - m_SkillMod.m_Mods[CCreatureSkillMod::SafeHeight].m_Diff;
            if (effectiveHeight <= 0.0) {
                effectiveHeight = 0.0;
            }
            if (IsPC() && m_data->m_moveType != CMT_FLY && !m_AreaMod.m_bIsUnderWater) {
                const double scaledHeight = effectiveHeight / 10.0;
                m_AreaMod.m_dHeightDamage = scaledHeight * scaledHeight;
            }
        }
    }
    return 0.0;
}

void CCreature::ChangeMoveType(::MoveType moveType, unsigned int reason) {
    L2_GUARD_FUNCTION();

    if (IsPC() && IsRiding()) {
        return;
    }
    if (runtime::runtime_timing::Matches(this)) {
        runtime::runtime_timing::Record(
            "move_mode_change", this, nullptr,
            "current=%d requested=%d reason=%u environment=%d "
            "base_speed=%d effective_speed=%.6f",
            static_cast<int>(m_data->m_MoveMode), static_cast<int>(moveType), reason,
            static_cast<int>(m_data->m_Environment), m_data->m_nMoveTypeSpeed, m_data->m_dParam[VALIDATE::SPEED]
        );
    }
    if (m_data->m_MoveMode == moveType) {
        return;
    }

    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(m_data->m_MoveMode), static_cast<std::int32_t>(moveType), 0x54
    );

    const int environment = static_cast<std::int32_t>(m_data->m_Environment);
    const FVector position = m_data->GetPos();
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_MOTION);
    } else {
        ValidateChanged(VB_MOTION);
    }

    if (IsPC()) {
        ParamChanged(PCB_USER | PCB_CHAR_MOVETYPE);
    } else if (IsNPC()) {
        CastNPC()->m_bSpeedChanged = true;
    } else {
        ParamChanged();
    }

    if (!IsGMInvisible()) {
        // Send the move mode to nearby movement observers.
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, position, "cddd", 0x28, runtime::Int32FromBits(m_nId), static_cast<std::int32_t>(moveType),
            environment
        );
        return;
    }

    if (IsPC()) {
        CastUser()->GetSocket()->Send(
            "cddd", 0x28, runtime::Int32FromBits(m_nId), static_cast<std::int32_t>(moveType), environment
        );
    }
}

double CCreature::ApplyIncreaseLimit(CreaturePointType pointType, double& difference, double maximum, double current) {
    L2_GUARD_FUNCTION();
    if (difference == 0.0) {
        return 0.0;
    }

    const CCreatureSkillMod::ModLimit& limit = m_SkillMod.m_ModsRegenLimit[static_cast<int>(pointType)];
    double boundary = maximum;
    if (difference < 0.0) {
        boundary = 0.0;
    }

    if (limit.m_Type != SFCT_NONE) {
        if (difference > 0.0) {
            if (limit.m_Type == SFCT_PER) {
                boundary = (limit.m_Limit / 100.0) * maximum;
            } else if (limit.m_Type == SFCT_DIFF) {
                boundary = limit.m_Limit;
                if (maximum < boundary) {
                    boundary = maximum;
                }
            } else {
                return 0.0;
            }
        } else {
            boundary = 0.0;
        }
    }

    double exceeded = 0.0;
    if (pointType == CPT_HP && boundary > 0.0) {
        exceeded = difference - (boundary - current);
        if (exceeded < 0.0) {
            exceeded = 0.0;
        } else if (m_data->m_dBaseCP < exceeded) {
            exceeded = m_data->m_dBaseCP;
        }
    }

    const double changed = difference + current;
    if (difference > 0.0) {
        if (boundary < changed) {
            difference = boundary - current;
            if (difference <= 0.0) {
                difference = 0.0;
            }
        }
    } else if (changed < boundary) {
        difference = boundary - current;
        if (difference >= 0.0) {
            difference = 0.0;
            exceeded = 0.0;
        }
    }
    return exceeded;
}

double CCreature::IncreaseCP_diff(double difference, int reason, unsigned int objectId, bool force) {
    const double current = m_data->m_dCP;
    ApplyIncreaseLimit(CPT_CP, difference, m_data->m_dBaseCP, current);
    if (difference != 0.0) {
        double changed = current + difference;
        if (changed >= 0.0) {
            if (m_data->m_dBaseCP < changed) {
                changed = m_data->m_dBaseCP;
            }
        } else {
            changed = 0.0;
        }
        m_data->SetCP(changed);
        if (IsPC()) {
            static_cast<User*>(this)->SendUserHPInfo(reason, objectId, force);
        }
    }
    return difference;
}

double CCreature::DecreaseCP_diff(double difference, int reason, unsigned int objectId) {
    L2_GUARD_FUNCTION();

    const double damage = GetDamageByLimit(difference);
    double changedCP = m_data->m_dCP - damage;
    if (changedCP >= 0.0) {
        m_data->SetCP(changedCP);
    } else {
        double changedHP = m_data->m_dHP + changedCP;
        if (changedHP < 1.0) {
            changedHP = 1.0;
        }
        m_data->SetCP(0.0);
        m_data->SetHP(changedHP);
    }
    m_data->ValidateCP(0.0);

    if (IsPC()) {
        int updateReason = 4;
        if (reason == 6) {
            if (damage > 0.0) {
                OnGotDamage(nullptr, damage);
            }
            updateReason = 6;
        }
        static_cast<User*>(this)->SendUserHPInfo(updateReason, objectId, false);
        m_data->m_NeedUpdatePacketBitSet.ClearBit(11);
    }
    return damage;
}

double CCreature::DecreaseCP_per(double ratio, int reason, unsigned int objectId) {
    L2_GUARD_FUNCTION();
    return DecreaseCP_diff(m_data->m_dCP * ratio, reason, objectId);
}

double CCreature::IncreaseHP_diff(double difference, bool broadcast, int reason, unsigned int objectId, bool force) {
    const double current = m_data->m_dHP;
    ApplyIncreaseLimit(CPT_HP, difference, m_data->m_dBaseHP, current);
    if (difference < 0.0) {
        difference = GetDamageByLimit(difference);
    }

    if (difference != 0.0) {
        double changed = current + difference;
        if (changed < 1.0) {
            changed = 1.0;
        }
        m_data->SetHP(changed);

        if (IsPC() && reason == 6 && difference <= 0.0 && difference != 0.0) {
            OnGotDamage(nullptr, -difference);
        }
        if (m_data->m_bIsAlive == 0) {
            m_data->SetHP(0.0);
        }

        if (!IsPC()) {
            SendStatusUpdate(VCP_HP, reason, objectId);
        } else {
            if (reason == 6) {
                force = false;
            }
            static_cast<User*>(this)->SendUserHPInfo(reason, objectId, force);
        }
        m_data->m_NeedUpdatePacketBitSet.ClearBit(11);
    }

    if (broadcast) {
        BroadcastHP(reason, objectId);
    }
    return difference;
}

double CCreature::IncreaseMP_diff(double difference, int reason, unsigned int objectId, bool force) {
    const double current = m_data->m_dMP;
    ApplyIncreaseLimit(CPT_MP, difference, m_data->m_dBaseMP, current);
    if (difference != 0.0) {
        m_data->SetMP(current + difference);
        m_data->ValidateMP(0.0);
        if (!IsPC()) {
            SendStatusUpdate(VCP_MP, reason, objectId);
        } else {
            CastUser()->SendUserMPInfo(reason, objectId, force);
        }
        m_data->m_NeedUpdatePacketBitSet.ClearBit(12);
    }
    return difference;
}

double CCreature::IncreaseCP_per(double ratio, int reason, unsigned int objectId) {
    return IncreaseCP_diff(m_data->m_dCP * ratio, reason, objectId, false);
}

double CCreature::IncreaseHP_per(double ratio, bool broadcast, int reason, unsigned int objectId) {
    return IncreaseHP_diff(m_data->m_dHP * ratio, broadcast, reason, objectId, false);
}

double CCreature::IncreaseMP_per(double ratio, int reason, unsigned int objectId) {
    return IncreaseMP_diff(m_data->m_dMP * ratio, reason, objectId, false);
}

bool CCreature::CanBeBuffed(const CSkillInfo* skillInfo, CCreature* source) {
    L2_GUARD_FUNCTION();

    const DebuffType debuffType = skillInfo->m_eDebuff;
    const AbnormalStatusSlotType abnormalType = skillInfo->m_nAbnormalType;
    if (m_SkillMod.m_bBlockBuff && debuffType == DB_GOOD && !skillInfo->m_bExceptBlock) {
        return false;
    }

    const bool debuffTypeOutsideBlockedRange =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(debuffType) - static_cast<std::int32_t>(DB_BAD)) > 2U;
    const bool specialLevelBypassesBlock = static_cast<std::uint32_t>(skillInfo->m_nSpecialLevel) + 4U < 4U;
    if ((!specialLevelBypassesBlock && CheckBlockDebuff(source) && !debuffTypeOutsideBlockedRange) ||
        (source != nullptr && m_nDamageExceptAttackerId != 0 && m_nDamageExceptAttackerId != source->m_nId &&
            !debuffTypeOutsideBlockedRange)) {
        SendMagicAttckInfo(source, this, MagicAttackInfo::ImmuneForBadSkill);
        return false;
    }

    L2_GUARD(m_SkillMod.m_Lock);
    if (m_SkillMod.m_BlockedSlotType.find(abnormalType) == m_SkillMod.m_BlockedSlotType.end() &&
        m_SkillMod.m_BlockedAddSlotType.find(abnormalType) == m_SkillMod.m_BlockedAddSlotType.end()) {
        return true;
    }

    if (debuffType != DB_GOOD) {
        SendMagicAttckInfo(source, this, MagicAttackInfo::ImmuneForBadSkill);
    }
    return false;
}

bool CCreature::CheckBlockDebuff(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (!m_SkillMod.m_bBlockDeBuff) {
        return false;
    }
    if (!creature) {
        return true;
    }

    const FVector sourcePosition = creature->GetPos();
    const FVector currentPosition = GetPos();
    const double deltaX = currentPosition.x - sourcePosition.x;
    const double deltaY = currentPosition.y - sourcePosition.y;
    const double deltaZ = currentPosition.z - sourcePosition.z;
    const double squaredDistance = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
    return squaredDistance >= m_SkillMod.m_dBlockDeBuffDist;
}

void CCreature::AbandonAttackLeaderOf(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    for (std::atomic<std::uint32_t>& attackLeaderId : creature->m_attackLeaderId) {
        std::uint32_t expected = m_nId;
        attackLeaderId.compare_exchange_strong(expected, std::uint32_t{0});
    }
}

void CCreature::ResetPassingPos(FVector position) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(PassingPosSect);
        m_nCurrentPassingPos = 0;
        m_nCurrentPassingPosSize = 0;
        for (FVector& passingPosition : m_vPassingPos) {
            passingPosition = position;
        }
    }
}

void CCreature::AddPassingPos(const FVector& position) {
    L2_GUARD_FUNCTION();
    L2_GUARD(PassingPosSect);

    const int previousIndex = (static_cast<int>(m_nCurrentPassingPos) + 14) % 15;
    const int olderIndex = (static_cast<int>(m_nCurrentPassingPos) + 13) % 15;
    const bool useNewAddPassingPos = g_bNewAddPassingPos;
    const FVector previousPosition = m_vPassingPos[previousIndex];
    const FVector olderPosition = m_vPassingPos[olderIndex];
    const double deltaX = previousPosition.x - position.x;
    const double deltaY = previousPosition.y - position.y;

    if (deltaX * deltaX + deltaY * deltaY < 1048576.0) {
        CGeoData* const geo = CWorld::Inst().GetGeo();
        const bool straight = geo->MoveStraightTest(
            olderPosition, position, 4096.0, m_data->IsFlying() || m_AreaMod.m_bIsUnderWater, m_AreaMod.m_nWaterSurfaceHeight
        );
        if (useNewAddPassingPos) {
            if (straight) {
                m_vPassingPos[previousIndex] = position;
                return;
            }
        } else if (!straight) {
            return;
        }
    }

    m_vPassingPos[m_nCurrentPassingPos] = position;
    m_nCurrentPassingPos = static_cast<std::int16_t>((static_cast<int>(m_nCurrentPassingPos) + 1) % 15);
    m_nCurrentPassingPosSize = static_cast<std::int16_t>(std::min(static_cast<int>(m_nCurrentPassingPosSize) + 1, 15));
}

std::uint64_t CCreature::GetPassingPosList(FVector* positions) {
    L2_GUARD_FUNCTION();
    std::uint64_t copied;
    {
        L2_GUARD(PassingPosSect);

        int currentPosition = (static_cast<int>(m_nCurrentPassingPos) + 14) % 15;
        std::int16_t count = m_nCurrentPassingPosSize > 15 ? std::int16_t{15} : m_nCurrentPassingPosSize;
        copied = 0;
        while (static_cast<int>(copied) < count) {
            positions[static_cast<int>(copied)] = m_vPassingPos[currentPosition];
            currentPosition = (currentPosition + 14) % 15;
            ++copied;
            count = m_nCurrentPassingPosSize > 15 ? std::int16_t{15} : m_nCurrentPassingPosSize;
        }
    }
    return copied;
}

FVector CCreature::GetPassingPosOnSight(
    const FVector& source, const FVector& destination, PathFindResult& result, CCreature* creature
) {
    L2_GUARD_FUNCTION();

    const bool floating = (static_cast<std::int32_t>(m_data->m_moveType) == CMT_FLY || m_AreaMod.m_bIsUnderWater) &&
        (static_cast<std::int32_t>(creature->m_data->m_moveType) == CMT_FLY || creature->m_AreaMod.m_bIsUnderWater);
    const int waterSurface = floating ? creature->m_AreaMod.m_nWaterSurfaceHeight : 0;
    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo->MoveStraightTest(source, destination, 4096.0, floating, waterSurface)) {
        result = PFR_PATH_FOUND;
        return destination;
    }

    FVector passingPositions[15]{};
    const std::uint64_t passingPositionCount = GetPassingPosList(passingPositions);
    for (std::uint64_t index = 0; index < passingPositionCount; ++index) {
        const FVector passingPosition = passingPositions[index];
        if (geo->MoveStraightTest(source, passingPosition, 4096.0, floating, waterSurface) &&
            geo->MoveStraightTest(passingPosition, GetPos(), 4096.0, floating, waterSurface)) {
            result = PFR_PATH_FOUND;
            return passingPosition;
        }
    }

    FVector pathDestination{};
    if (g_bMoveToLeader && creature != nullptr && !creature->IsSummonOrPet() && !creature->IsBoss()) {
        const Position position = creature->GetMyPositionFrom(this);
        CCreature* leader = g_CreatureSmartIdManager.GetObjectW(m_attackLeaderId[static_cast<int>(position)].load());
        if (leader == creature) {
            leader = nullptr;
        }
        if (leader != nullptr && leader->IsValidAttackLeaderCandidate()) {
            FVector leaderPassingPositions[15]{};
            const std::uint64_t leaderPositionCount = leader->GetPassingPosList(leaderPassingPositions);
            const bool leaderFloating =
                (static_cast<std::int32_t>(leader->m_data->m_moveType) == CMT_FLY || leader->m_AreaMod.m_bIsUnderWater) &&
                (static_cast<std::int32_t>(creature->m_data->m_moveType) == CMT_FLY || creature->m_AreaMod.m_bIsUnderWater);
            const int leaderWaterSurface = leaderFloating ? creature->m_AreaMod.m_nWaterSurfaceHeight : 0;
            for (std::uint64_t index = 0; index < leaderPositionCount; ++index) {
                if (geo->MoveStraightTest(
                        source, leaderPassingPositions[index], 4096.0, leaderFloating, leaderWaterSurface
                    )) {
                    result = PFR_PATH_FOUND;
                    return leaderPassingPositions[index];
                }
            }
            if (leaderPositionCount != 0) {
                pathDestination = leaderPassingPositions[0];
            } else {
                pathDestination = GetDetailLocation();
            }
        } else {
            pathDestination = GetDetailLocation();
        }
    } else {
        pathDestination = GetDetailLocation();
    }

    FVector output{};
    result = g_pnWorld.PathFind(source, pathDestination, output, 1000, creature);
    if (result == PFR_ERROR) {
        for (std::uint64_t index = 0; index < passingPositionCount; ++index) {
            const FVector passingPosition = passingPositions[index];
            if (geo->MoveStraightTest(source, passingPosition, 4096.0, floating, waterSurface)) {
                result = PFR_PATH_FOUND;
                output = passingPosition;
                break;
            }
        }
    }
    return output;
}

bool CCreature::BeClosedToPassingPos(const FVector& position) {
    L2_GUARD_FUNCTION();
    L2_GUARD(PassingPosSect);

    const std::int16_t count = m_nCurrentPassingPosSize > 15 ? std::int16_t{15} : m_nCurrentPassingPosSize;
    for (int index = 0; index < count; ++index) {
        const double deltaX = m_vPassingPos[index].x - position.x;
        const double deltaY = m_vPassingPos[index].y - position.y;
        const double deltaZ = m_vPassingPos[index].z - position.z;
        const double squaredDistance = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        if (squaredDistance < 1600.0) {
            return true;
        }
    }

    return false;
}

CCreature::Position CCreature::GetMyPositionFrom(const CCreature* creature) const {
    L2_GUARD_FUNCTION();
    const FVector sourcePosition = creature->GetPos();
    const FVector currentPosition = GetPos();
    const std::int32_t deltaY = runtime::SubtractInt32Wrapping(
        runtime::Win64CvttDoubleToInt32(currentPosition.y), runtime::Win64CvttDoubleToInt32(sourcePosition.y)
    );
    const std::int32_t deltaX = runtime::SubtractInt32Wrapping(
        runtime::Win64CvttDoubleToInt32(currentPosition.x), runtime::Win64CvttDoubleToInt32(sourcePosition.x)
    );
    const double direction = std::atan2(static_cast<double>(deltaY), static_cast<double>(deltaX));
    const int directionStep = runtime::Win64CvttDoubleToInt32((direction / 3.141592) * -2.0);
    int position = 2 - directionStep;
    if (position > POSITION_4) {
        position = POSITION_4;
    }
    if (position < POSITION_1) {
        position = POSITION_1;
    }
    return static_cast<Position>(position);
}

bool CCreature::IsCloserThan(CCreature* creature, const FVector& position) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return true;
    }

    const FVector currentPosition = GetPos();
    const FVector creaturePosition = creature->GetPos();
    if (currentPosition.DistCube(position) >= creaturePosition.DistCube(position)) {
        return false;
    }

    const bool floating = static_cast<std::int32_t>(m_data->m_moveType) == CMT_FLY || m_AreaMod.m_bIsUnderWater;
    const int waterSurface =
        m_AreaMod.m_bIsUnderWater && creature->m_AreaMod.m_bIsUnderWater ? m_AreaMod.m_nWaterSurfaceHeight : 0;
    return CWorld::Inst().GetGeo()->MoveStraightTest(currentPosition, creaturePosition, 4096.0, floating, waterSurface);
}

bool CCreature::IsValidAttackLeaderCandidate() {
    return !IsFrozen(true) && m_SkillMod.m_nBlockMoveCount == 0 && m_data->m_bCanMove && m_SkillMod.m_bCanAct && IsAlive() &&
        !IsSummonOrPet() && !IsPet();
}

bool CCreature::SetAttackLeaderRelated(CCreature* creature, CCreature* expectedCreature) {
    if (creature == nullptr) {
        return false;
    }
    const Position position = creature->GetMyPositionFrom(this);
    std::uint32_t expectedId = expectedCreature != nullptr ? expectedCreature->m_nId : 0;
    return m_attackLeaderId[static_cast<int>(position)].compare_exchange_strong(expectedId, creature->m_nId);
}

bool CCreature::GetStatusValue(CHAR_PARAM_TYPE parameter, int& value) {
    L2_GUARD_FUNCTION();
    switch (parameter) {
        case VCP_HP:
            value = runtime::Win64CvttDoubleToInt32(m_data->m_dHP);
            return true;
        case VCP_MAXHP:
            value = runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP);
            return true;
        case VCP_MP:
            value = runtime::Win64CvttDoubleToInt32(m_data->m_dMP);
            return true;
        case VCP_ISGUILTY:
            value = static_cast<int>(static_cast<signed char>(GetGuilty()));
            return true;
        case VCP_CRIMINAL_RATE:
            value = CalcCriminalRate();
            return true;
        case VCP_CP:
            value = runtime::Win64CvttDoubleToInt32(m_data->m_dCP);
            return true;
        case VCP_ULTIMATE_SKILL_POINT:
            value = m_data->m_UltimateSkillGauge;
            return true;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "VCP type [%d] not supported yet in file[%s], line[%d]",
            //     static_cast<int>(parameter),
            //     "Creature_server.cpp",
            //     0x52a9)
            // Log.Add(
            //     LOG_ERROR,
            //     "[stats] Status parameter %d is unsupported for object %u. Returning failure with value 0.",
            //     static_cast<int>(parameter),
            //     m_nId)
            L2_LOG_ERROR(
                Log, "[stats] Status parameter %d is unsupported for object %u; returning failure with value 0.",
                static_cast<int>(parameter), m_nId
            );
            value = 0;
            return false;
    }
}

void CCreature::SendStatusUpdate(const CHAR_PARAM_TYPE parameter, const int reason, const unsigned int objectId) {
    // Current L2Server 0x00609120.
    L2_GUARD_FUNCTION();

    if (IsPC()) {
        User* const user = CastUser();
        if (parameter == VCP_HP) {
            user->SendUserHPInfo(reason, static_cast<int>(objectId), false);
            return;
        }
        if (parameter == VCP_MP) {
            user->SendUserMPInfo(reason, static_cast<int>(objectId), false);
            return;
        }
    }

    int statusValue = 0;
    if (!GetStatusValue(parameter, statusValue)) {
        return;
    }

    const FVector position = GetPos();
    const int packetReason = static_cast<int>(static_cast<std::int8_t>(reason));
    const int packetParameter = static_cast<int>(static_cast<std::int8_t>(parameter));

    if (!IsPC()) {
        if (IsSummonOrPet()) {
            if (parameter == VCP_HP || parameter == VCP_MP) {
                User* const master = CastSummon()->GetMaster();
                if (master != nullptr && master->GetSocket() != nullptr) {
                    master->GetSocket()->Send(
                        "cddcccd", 0x18, runtime::Int32FromBits(m_nId), static_cast<int>(objectId), packetReason, 1,
                        packetParameter, statusValue
                    );
                    if (CParty* const party = master->GetParty()) {
                        party->UpdatePetWindowPacket(CastSummon());
                    }
                }
            } else {
                CastSummon()->SendSummonStatusToMaster(0);
            }
        }

        if (!g_bSendSummonHPMPInfo && IsSummon()) {
            g_ptlUserMap->BroadcastToNeighborParty(
                this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId),
                static_cast<int>(objectId), packetReason, 1, packetParameter, statusValue
            );
            g_ptlUserMap->BroadcastToLawfulPVPMember(
                this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId),
                static_cast<int>(objectId), packetReason, 1, packetParameter, statusValue
            );
        } else {
            g_ptlUserMap->BroadcastToTargettingNeighbor(
                this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId),
                static_cast<int>(objectId), packetReason, 1, packetParameter, statusValue
            );
        }
        return;
    }

    User* const user = CastUser();
    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0x80000, position, 0x1000, "cddcccd", 0x18, runtime::Int32FromBits(m_nId), static_cast<int>(objectId),
            packetReason, 1, packetParameter, statusValue
        );
        return;
    }

    if (user != nullptr && user->GetSocket() != nullptr) {
        user->GetSocket()->Send(
            "cddcccd", 0x18, runtime::Int32FromBits(m_nId), static_cast<int>(objectId), packetReason, 1, packetParameter,
            statusValue
        );
    }
}

bool CCreature::SetShowNametag(bool show) {
    L2_GUARD_FUNCTION();
    if (m_data->m_StatusFlag[1] == show) {
        return false;
    }

    m_data->m_StatusFlag[1] = show;
    if (IsPC()) {
        ParamChanged(std::int64_t{PCB_CHAR_SHOWNAMETAG});
    } else {
        ParamChanged();
    }
    return true;
}

bool CCreature::IsTalkableDistance(CWorldObject* object) const {
    L2_GUARD_FUNCTION();
    if (object == nullptr) {
        return false;
    }

    const FVector targetPosition = object->GetPos();
    const FVector currentPosition = GetPos();
    return (currentPosition.x - targetPosition.x) * (currentPosition.x - targetPosition.x) +
        (currentPosition.y - targetPosition.y) * (currentPosition.y - targetPosition.y) <=
        62500.0;
}

int CCreature::CriticalRateForInfo(VALIDATE::Enum type) const {
    L2_GUARD_FUNCTION();
    const double criticalRate = m_data->m_dParam[static_cast<int>(type)];
    return runtime::Win64CvttDoubleToInt32(criticalRate * 10.0 + 0.5);
}

bool CCreature::IsCheckBot1(int first, int second) {
    L2_GUARD_FUNCTION();
    const std::int32_t expected = runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(first, -0xd6a), -0x14e9);
    return expected == second;
}

bool CCreature::IsCheckBot2(int first, int second) {
    L2_GUARD_FUNCTION();
    const std::int32_t expected = runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(first, 0x79), -4000000);
    return expected == second;
}

bool CCreature::IsCheckBot3(int value) {
    L2_GUARD_FUNCTION();
    const std::int32_t lowerDelta = runtime::AddInt32Wrapping(value, -0x3042);
    if (lowerDelta < 0) {
        return false;
    }
    return runtime::AddInt32Wrapping(value, -0x41d2) < 0;
}

bool CCreature::IsPvpTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    return creature != nullptr && IsPCOrPCRelatedNPC() && creature->IsPCOrPCRelatedNPC();
}

SlotType CCreature::IsEquiped(int itemIndex, SlotType slotType) {
    L2_GUARD_FUNCTION();

    const auto findInRange = [this, itemIndex](int first, int last) {
        for (int slot = first; slot <= last; ++slot) {
            if (m_data->m_nSlotIndex[slot] == itemIndex) {
                return static_cast<SlotType>(slot);
            }
        }
        return ST_MAX;
    };

    switch (slotType) {
        case ST_REAR:
        case ST_LEAR:
            return findInRange(ST_REAR, ST_LEAR);
        case ST_RFINGER:
        case ST_LFINGER:
            return findInRange(ST_RFINGER, ST_LFINGER);
        case ST_RHAND:
        case ST_LHAND:
        case ST_RLHAND:
            if (m_data->m_nSlotIndex[ST_RHAND] == itemIndex) {
                return ST_RHAND;
            }
            if (m_data->m_nSlotIndex[ST_LHAND] == itemIndex) {
                return ST_LHAND;
            }
            return m_data->m_nSlotIndex[ST_RLHAND] == itemIndex ? ST_RLHAND : ST_MAX;
        case ST_CHEST:
        case ST_LEGS:
        case ST_ONEPIECE:
            if (m_data->m_nSlotIndex[ST_CHEST] == itemIndex) {
                return ST_CHEST;
            }
            if (m_data->m_nSlotIndex[ST_LEGS] == itemIndex) {
                return ST_LEGS;
            }
            return m_data->m_nSlotIndex[ST_ONEPIECE] == itemIndex ? ST_ONEPIECE : ST_MAX;
        case ST_HAIR:
        case ST_HAIR2:
        case ST_HAIRALL:
            if (m_data->m_nSlotIndex[ST_HAIR] == itemIndex) {
                return ST_HAIR;
            }
            if (m_data->m_nSlotIndex[ST_HAIR2] == itemIndex) {
                return ST_HAIR2;
            }
            return m_data->m_nSlotIndex[ST_HAIRALL] == itemIndex ? ST_HAIRALL : ST_MAX;
        default:
            break;
    }

    const int slot = static_cast<int>(slotType);
    if (slot >= ST_AGATHION_MAIN && slot <= ST_AGATHION_SUB4) {
        return findInRange(ST_AGATHION_MAIN, ST_AGATHION_SUB4);
    }
    if (slot >= ST_DECO1 && slot <= ST_DECO6) {
        return findInRange(ST_DECO1, ST_DECO6);
    }
    if (slot >= ST_JEWEL1 && slot <= ST_JEWEL6) {
        return findInRange(ST_JEWEL1, ST_JEWEL6);
    }
    if (slot >= ST_ARTIFACT_A1 && slot <= ST_ARTIFACT_A12) {
        return findInRange(ST_ARTIFACT_A1, ST_ARTIFACT_A12);
    }
    if (slot >= ST_ARTIFACT_B1 && slot <= ST_ARTIFACT_B3) {
        return findInRange(ST_ARTIFACT_B1, ST_ARTIFACT_B3);
    }
    if (slot >= ST_ARTIFACT_C1 && slot <= ST_ARTIFACT_C3) {
        return findInRange(ST_ARTIFACT_C1, ST_ARTIFACT_C3);
    }
    if (slot >= ST_ARTIFACT_D1 && slot <= ST_ARTIFACT_D3) {
        return findInRange(ST_ARTIFACT_D1, ST_ARTIFACT_D3);
    }
    if (slot >= ST_UNDERWEAR && slot < ST_MAX && m_data->m_nSlotIndex[slot] == itemIndex) {
        return slotType;
    }
    return ST_MAX;
}

bool CCreature::IsEquipedItem(int itemIndex) {
    L2_GUARD_FUNCTION();
    for (int slotIndex : m_data->m_nSlotIndex) {
        if (slotIndex == itemIndex) {
            return true;
        }
    }
    return false;
}

bool CCreature::IsEquipedItemByClassId(int classId) {
    L2_GUARD_FUNCTION();
    for (int slotIndex : m_data->m_nSlotIndex) {
        CSharedItemData* const item = CItemFactory::Inst().GetData(slotIndex);
        if (item && item->m_nClassID == classId) {
            return true;
        }
    }
    return false;
}

bool CCreature::IsEquippedWithForcedItem(SlotType slotType) {
    L2_GUARD_FUNCTION();
    if (slotType == ST_RHAND || slotType == ST_LHAND) {
        CItem* const twoHanded = m_inv.GetByIndex(m_data->m_nSlotIndex[ST_RLHAND]);
        if (twoHanded != nullptr && twoHanded->m_const->m_bForceEquip) {
            return true;
        }
    } else if (slotType == ST_RLHAND) {
        CItem* const rightHand = m_inv.GetByIndex(m_data->m_nSlotIndex[ST_RHAND]);
        CItem* const leftHand = m_inv.GetByIndex(m_data->m_nSlotIndex[ST_LHAND]);
        if ((rightHand != nullptr && rightHand->m_const->m_bForceEquip) ||
            (leftHand != nullptr && leftHand->m_const->m_bForceEquip)) {
            return true;
        }
    }

    CItem* const item = m_inv.GetByIndex(m_data->m_nSlotIndex[static_cast<int>(slotType)]);
    return item != nullptr && item->m_const->m_bForceEquip;
}

bool CCreature::IsDebuffExist() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    const bool exists = !m_InfectedSkills.empty();
    return exists;
}

std::uint64_t CCreature::GetAbnormalCount() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InfectedSkillsSect);
    const std::uint64_t count = m_InfectedSkills.size();
    return count;
}

void CCreature::PushCreatureAfterCheckRangeAndCanSee(
    CCreature* creature,
    CCreature* target,
    int affectLimitBase,
    int affectLimitRandom,
    bool affectLimitToPc,
    int& targetCount,
    SkillTargetData& targetData,
    double distance,
    bool checkCanSee,
    FVector position
) {
    L2_GUARD_FUNCTION();
    if (target == nullptr || creature == nullptr) {
        return;
    }

    const bool isPcOrSummon = target->IsPCOrSummon();
    if (!isPcOrSummon) {
        if (affectLimitRandom > 0 && affectLimitRandom < targetCount) {
            return;
        }
    } else {
        if (!creature->m_AreaMod.m_bIsInBattleField || !target->m_AreaMod.m_bIsInBattleField) {
            if (affectLimitBase == 2) {
                if (target->m_data->m_nKarma >= 0) {
                    return;
                }
            } else if (affectLimitBase == 3) {
                return;
            }
        }
        if (affectLimitToPc && affectLimitRandom > 0 && affectLimitRandom < targetCount) {
            return;
        }
    }

    FVector checkPosition{};
    if (position.IsEqual(checkPosition)) {
        checkPosition = creature->GetPos();
    } else {
        checkPosition = position;
    }

    {
        GuardHelper canSeeCheck(L"CanSeeCheck", CThreadManager::GetThreadID());
        if (checkCanSee && g_CheckCollisionForRangeSkill) {
            CWorld& world = CWorld::Inst();
            const int inZoneId = static_cast<int>(creature->GetInZoneID());
            const FVector targetPosition = target->GetPos();
            CGeoData* const geo = world.GetGeo();
            if (!geo->CanSee(checkPosition, targetPosition, inZoneId, true)) {
                return;
            }
        }
    }

    if (target->CastNPC() == nullptr || !target->CastNPC()->IsFakeNPC()) {
        {
            GuardHelper targetDataCheck(L"targetDataCheck", CThreadManager::GetThreadID());
            const CSafePointer2<CCreature> safeTarget(target);
            targetData.c.push_back(safeTarget);
            targetData.d.push_back(distance);
            g_nSkillTargetCount[CThreadManager::GetThreadID()] =
                runtime::Int32FromBits(static_cast<std::uint32_t>(targetData.d.size()));
        }
        if (!isPcOrSummon || affectLimitToPc) {
            targetCount = runtime::AddInt32Wrapping(targetCount, 1);
        }
    }
}

void CCreature::PopCreatureAfterCheckRangeAndCanSee(
    CCreature* creature,
    CCreature* target,
    const int rangeTargetType,
    const int affectLimit,
    int& targetCount,
    SkillTargetData& targetData,
    const double distance,
    const bool checkCanSee,
    FVector position
) {
    // Current L2Server 0x0060f808. Unlike the Push variant, the target
    // counter is not advanced by this shipped body.
    L2_GUARD_FUNCTION();
    if (target == nullptr || creature == nullptr) {
        return;
    }

    if (target->IsPCOrSummon()) {
        if ((!creature->m_AreaMod.m_bIsInBattleField || !target->m_AreaMod.m_bIsInBattleField)) {
            if (rangeTargetType == RTT_CHAO) {
                if (target->m_data->m_nKarma >= 0) {
                    return;
                }
            } else if (rangeTargetType == RTT_PEACE_ZONE) {
                return;
            }
        }
    } else if (affectLimit > 0 && affectLimit < targetCount) {
        return;
    }

    if (position.IsZero()) {
        position = creature->GetPos();
    }
    if (checkCanSee && g_CheckCollisionForRangeSkill &&
        !CWorld::Inst().GetGeo()->CanSee(position, target->GetPos(), static_cast<int>(creature->GetInZoneID()), true)) {
        return;
    }

    if (target->CastNPC() == nullptr || !target->CastNPC()->IsFakeNPC()) {
        targetData.c.push_back(CSafePointer2<CCreature>(target));
        targetData.d.push_back(distance);
    }
}

bool CCreature::CheckIsRealFriend(CCreature* first, CCreature* second) {
    User* const firstUser = first->GetUserOrMaster();
    User* const secondUser = second->GetUserOrMaster();
    if (firstUser == nullptr || secondUser == nullptr) {
        return false;
    }
    if (firstUser->IsEnemyTo(secondUser)) {
        return false;
    }

    if ((firstUser->m_data->m_nPledgeID != 0 && firstUser->m_data->m_nPledgeID == secondUser->m_data->m_nPledgeID) ||
        firstUser->IsMyParty(secondUser)) {
        return true;
    }

    CPledge* const firstPledge = firstUser->GetPledge();
    CPledge* const secondPledge = secondUser->GetPledge();
    if (firstPledge == nullptr || secondPledge == nullptr) {
        return false;
    }

    const std::uint32_t allianceSid = firstPledge->GetAllianceSid();
    return allianceSid != 0 && allianceSid == secondPledge->GetAllianceSid();
}

bool CCreature::CheckRelationWithCaster(
    CCreature* caster, CCreature* target, SkillAffectObjectTypeEnum affectObjectType, bool requireBattleField
) {
    L2_GUARD_FUNCTION();

    if ((target->IsNPC() && !target->IsTargetable()) ||
        (target->IsPCOrPCRelatedNPC() &&
            ((caster->IsPCOrPCRelatedNPC() &&
                 (affectObjectType == SAFOT_NOT_FRIEND || affectObjectType == SAFOT_NOT_FRIEND_PC) &&
                 !AreaBasedRule::BotReport(target)) ||
                target->IsGMInvisible())) ||
        caster->m_data->m_nPrivateID != target->m_data->m_nPrivateID) {
        return false;
    }
    if (requireBattleField && !target->m_AreaMod.m_bIsInBattleField) {
        return false;
    }

    if (affectObjectType == SAFOT_NPC_DEAD_BODY) {
        if (target->IsNPC() && !target->IsSummonOrPet()) {
            CNPC* const npc = target->CastNPC();
            return !npc->IsAlive();
        }
        return false;
    }

    if (affectObjectType == SAFOT_INVISIBLE) {
        if (target->IsVisible()) {
            return false;
        }
        if (!target->IsNPC()) {
            return target->IsPC() && target->IsInvisibleBySkill() && target->IsEnemyTo(caster);
        }

        CNPC* const npc = target->CastNPC();
        if (npc->GetMaster() != nullptr) {
            return target->IsEnemyTo(caster);
        }
        return true;
    }

    if (affectObjectType == SAFOT_NEUTRAL_INVISIBLE) {
        if (target->IsVisible()) {
            return false;
        }
        User* const targetUser = target->GetUserOrMaster();
        if (targetUser != nullptr && targetUser->IsGMInvisible()) {
            return false;
        }
        if (!target->IsNPC() && (!target->IsPC() || !target->IsInvisibleBySkill())) {
            return false;
        }

        User* const casterUser = caster->GetUserOrMaster();
        if (casterUser->IsInDuel()) {
            return casterUser->IsDuelEnemy(target);
        }
        return !CheckIsRealFriend(target, caster);
    }

    if (!target->IsAlive() || target->IsTrap()) {
        return false;
    }

    switch (affectObjectType) {
        case SAFOT_NONE:
            return true;
        case SAFOT_FRIEND:
            return !target->IsEnemyTo(caster);
        case SAFOT_NOT_FRIEND:
            return target->IsEnemyTo(caster);
        case SAFOT_UNDEAD_REAL_ENEMY:
            return target->IsEnemyTo(caster) && target->m_data->m_Race == RT_UNDEAD;
        case SAFOT_WYVERN_OBJECT:
            return !target->IsNPC() && target->IsEnemyTo(caster);
        case SAFOT_CLAN:
            return Singleton<CClanHelpTable>::inst().GetFlag(caster->m_data->m_nClassIndex, target->m_data->m_nClassIndex);
        case SAFOT_FRIEND_PC:
            return !target->IsEnemyTo(caster) && target->IsPCOrPCRelatedNPC();
        case SAFOT_NOT_FRIEND_PC:
            return target->IsEnemyTo(caster) && target->IsPCOrPCRelatedNPC();
        default:
            return true;
    }
}

void CCreature::GatherNeighborCreature(
    FVector position,
    int affectLimitBase,
    int affectLimitRandom,
    bool affectLimitToPc,
    SkillTargetData& targetData,
    int affectRange,
    int affectScopeMinZ,
    int affectScopeMaxZ,
    SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    CCreature* exceptCreature,
    bool checkCanSee,
    bool unknown
) {
    L2_GUARD_FUNCTION();

    struct FilterCond {
        FilterCond(
            CCreature* exceptCreature,
            CCreature* creature,
            SkillAffectObjectTypeEnum affectObjectType,
            bool unknown,
            bool checkZ,
            int bottomZ,
            int topZ
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            affectObjectType(affectObjectType),
            unknown(unknown),
            checkZ(checkZ),
            bottomZ(bottomZ),
            topZ(topZ) {}

        virtual bool CanDo(CCreature* target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, unknown)) {
                return false;
            }
            if (checkZ) {
                const int targetZ = runtime::Win64CvttDoubleToInt32(target->m_data->m_Pos.z);
                if (topZ < targetZ) {
                    return false;
                }
                const int collisionHeight = runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight);
                if (runtime::AddInt32Wrapping(collisionHeight, targetZ) < bottomZ) {
                    return false;
                }
            }
            return true;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        SkillAffectObjectTypeEnum affectObjectType;
        bool unknown;
        bool checkZ;
        int bottomZ;
        int topZ;
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(int inZoneId, const FVector& filterPosition, double distance, FilterCond* condition) :
            CTLMFilterDist(inZoneId, filterPosition, distance), condition(condition) {}

        bool CanDo(CObject* object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    };

    struct GatherData {
        CCreature* creature;
        int affectLimitBase;
        int affectLimitRandom;
        bool affectLimitToPc;
        int* targetCount;
        SkillTargetData* targetData;
        bool checkCanSee;
        FVector* position;
    };

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* data) : data(data) {}

        bool CanDo(CObject* object, double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PushCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->affectLimitBase, data->affectLimitRandom,
                    data->affectLimitToPc, *data->targetCount, *data->targetData, distance, data->checkCanSee,
                    *data->position
                );
            }
            return true;
        }

        GatherData* data;
    };


    const bool checkZ = affectScopeMinZ != 0 && affectScopeMaxZ != 0;
    int bottomZ = 0;
    int topZ = 0;
    if (checkZ) {
        const int positionZ = runtime::Win64CvttDoubleToInt32(position.z);
        bottomZ = runtime::AddInt32Wrapping(positionZ, affectScopeMinZ);
        topZ = runtime::AddInt32Wrapping(positionZ, affectScopeMaxZ);
    }
    FilterCond condition{
        exceptCreature,
        creature,
        affectObjectType,
        unknown,
        checkZ,
        bottomZ,
        topZ,
    };
    TLMFilter filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(affectRange), &condition);
    int targetCount = 0;
    GatherData data{
        creature,
        affectLimitBase,
        affectLimitRandom,
        affectLimitToPc,
        &targetCount,
        &targetData,
        checkCanSee,
        &position,
    };
    TLMFunc function(&data);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborCreatureMelee(
    const int rangeTargetType,
    const int affectLimit,
    const bool affectLimitToPc,
    SkillTargetData& targetData,
    const DamageRangeType& damageRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* const creature,
    CCreature* const exceptCreature,
    const FVector position,
    const int angle
) {
    // Current L2Server 0x005f54e0. The three local polymorphic
    // envelopes are created literally from the current vtables at
    // 0x00f1ec68/78/90 and their CanDo slots at
    // 0x005fe548/0x005ed5b4/0x0060fb3c.
    L2_GUARD_FUNCTION();

    const FVector actorPosition = creature->m_data->GetPos();
    const FVector aim{position.x - actorPosition.x, position.y - actorPosition.y, position.z - actorPosition.z};
    int yaw = creature->m_data->GetDir().Yaw;
    if (!aim.IsZero()) {
        yaw = runtime::Win64CvttDoubleToInt32(std::atan2(aim.y, aim.x) * 65535.0 / 6.283185307179586);
    }
    yaw = runtime::AddInt32Wrapping(
        yaw, runtime::Win64CvttDoubleToInt32((static_cast<double>(damageRange.direction) / 360.0) * 65535.0)
    );
    const FAngle begin{runtime::AddInt32Wrapping(yaw, -(angle / 2))};
    const FAngle end{runtime::AddInt32Wrapping(yaw, angle / 2)};

    const int actorZ = runtime::Win64CvttDoubleToInt32(actorPosition.z);
    int bottomZ = actorZ;
    int topZ =
        runtime::AddInt32Wrapping(actorZ, runtime::Win64CvttDoubleToInt32(creature->m_data->m_dCollisionHeight));
    if (affectScopeMinZ != 0 && affectScopeMaxZ != 0) {
        bottomZ = runtime::AddInt32Wrapping(actorZ, affectScopeMinZ);
        topZ = runtime::AddInt32Wrapping(actorZ, affectScopeMaxZ);
    }

    struct FilterCond {
        FilterCond(
            CCreature* const exceptCreature,
            CCreature* const creature,
            const SkillAffectObjectTypeEnum affectObjectType,
            const int bottomZ,
            const int topZ,
            const FVector* const actorPosition,
            const FAngle* const begin,
            const FAngle* const end
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            affectObjectType(affectObjectType),
            bottomZ(bottomZ),
            topZ(topZ),
            actorPosition(actorPosition),
            begin(begin),
            end(end) {}

        virtual bool CanDo(CCreature* const target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, false)) {
                return false;
            }

            const FVector targetPosition = target->m_data->GetPos();
            FAngle targetAngle{runtime::Win64CvttDoubleToInt32(
                std::atan2(targetPosition.y - actorPosition->y, targetPosition.x - actorPosition->x) * 65535.0 /
                6.283185307179586
            )};
            if (!targetAngle.IsBetween(*begin, *end)) {
                return false;
            }

            const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
            const int targetTop = runtime::AddInt32Wrapping(
                targetZ, runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight)
            );
            return targetZ <= topZ && bottomZ <= targetTop;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        SkillAffectObjectTypeEnum affectObjectType;
        int bottomZ;
        int topZ;
        const FVector* actorPosition;
        const FAngle* begin;
        const FAngle* end;
    } condition{
        exceptCreature,
        creature,
        affectObjectType,
        bottomZ,
        topZ,
        &actorPosition,
        &begin,
        &end,
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& actorPosition, const double radius, FilterCond* const condition) :
            CTLMFilterDist(inZoneId, actorPosition, radius), condition(condition) {}

        bool CanDo(CObject* const object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    } filter(static_cast<int>(GetInZoneID()), actorPosition, static_cast<double>(damageRange.distance), &condition);

    struct GatherData {
        CCreature* creature;
        int rangeTargetType;
        int affectLimit;
        bool affectLimitToPc;
        int* targetCount;
        SkillTargetData* targetData;
    } data{
        creature,
        rangeTargetType,
        affectLimit,
        affectLimitToPc,
        nullptr,
        &targetData,
    };
    int targetCount = 0;
    data.targetCount = &targetCount;

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* const data) : data(data) {}

        bool CanDo(CObject* const object, const double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PushCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->rangeTargetType, data->affectLimit, data->affectLimitToPc,
                    *data->targetCount, *data->targetData, distance, true, FVector{}
                );
            }
            return true;
        }

        GatherData* data;
    } function(&data);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborCreatureArc(
    const int rangeTargetType,
    const int affectLimit,
    const bool affectLimitToPc,
    SkillTargetData& targetData,
    const DamageRangeType& damageRange,
    int,
    int,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    CCreature* exceptCreature,
    const FVector position,
    const int angle
) {
    // Current L2Server 0x005f4c5c.
    L2_GUARD_FUNCTION();
    const FVector actorPosition = creature->GetPos();
    const FVector aim{position.x - actorPosition.x, position.y - actorPosition.y, position.z - actorPosition.z};
    int yaw = creature->m_data->GetDir().Yaw;
    if (!aim.IsZero()) {
        yaw = static_cast<int>(std::atan2(aim.y, aim.x) * 65535.0 / 6.283185307179586);
    }
    yaw += static_cast<int>((static_cast<double>(damageRange.direction) / 360.0) * 65535.0);
    const FAngle begin{yaw - angle / 2};
    const FAngle end{yaw + angle / 2};
    const int bottomZ = runtime::Win64CvttDoubleToInt32(actorPosition.z);
    const int topZ =
        runtime::AddInt32Wrapping(bottomZ, runtime::Win64CvttDoubleToInt32(creature->m_data->m_dCollisionHeight));

    struct FilterCond {
        FilterCond(
            CCreature* const exceptCreature,
            CCreature* const creature,
            const SkillAffectObjectTypeEnum affectObjectType,
            const int bottomZ,
            const int topZ,
            const FVector* const actorPosition,
            const FAngle* const begin,
            const FAngle* const end
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            affectObjectType(affectObjectType),
            bottomZ(bottomZ),
            topZ(topZ),
            actorPosition(actorPosition),
            begin(begin),
            end(end) {}

        virtual bool CanDo(CCreature* const target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, false)) {
                return false;
            }
            const FVector targetPosition = target->m_data->GetPos();
            FAngle targetAngle{runtime::Win64CvttDoubleToInt32(
                std::atan2(targetPosition.y - actorPosition->y, targetPosition.x - actorPosition->x) * 65535.0 /
                6.283185307179586
            )};
            if (!targetAngle.IsBetween(*begin, *end)) {
                return false;
            }
            const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
            const int targetTop = runtime::AddInt32Wrapping(
                targetZ, runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight)
            );
            return targetZ <= topZ && bottomZ <= targetTop;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        SkillAffectObjectTypeEnum affectObjectType;
        int bottomZ;
        int topZ;
        const FVector* actorPosition;
        const FAngle* begin;
        const FAngle* end;
    } condition{
        exceptCreature,
        creature,
        affectObjectType,
        bottomZ,
        topZ,
        &actorPosition,
        &begin,
        &end,
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& actorPosition, const double radius, FilterCond* const condition) :
            CTLMFilterDist(inZoneId, actorPosition, radius), condition(condition) {}

        bool CanDo(CObject* const object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    } filter(static_cast<int>(GetInZoneID()), actorPosition, static_cast<double>(damageRange.distance), &condition);

    struct GatherData {
        CCreature* creature;
        int rangeTargetType;
        int affectLimit;
        bool affectLimitToPc;
        int* targetCount;
        SkillTargetData* targetData;
    } data{
        creature,
        rangeTargetType,
        affectLimit,
        affectLimitToPc,
        nullptr,
        &targetData,
    };
    int targetCount = 0;
    data.targetCount = &targetCount;

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* const data) : data(data) {}

        bool CanDo(CObject* const object, const double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PushCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->rangeTargetType, data->affectLimit, data->affectLimitToPc,
                    *data->targetCount, *data->targetData, distance, true, FVector{}
                );
            }
            return true;
        }

        GatherData* data;
    } function(&data);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborCreatureSquare(
    const int rangeTargetType,
    const int affectLimit,
    const bool affectLimitToPc,
    SkillTargetData& targetData,
    const DamageRangeType& damageRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    CCreature* exceptCreature,
    const FVector position
) {
    // Current L2Server 0x005f4f00.
    L2_GUARD_FUNCTION();
    const FVector actorPosition = creature->GetPos();
    const FVector aim{position.x - actorPosition.x, position.y - actorPosition.y, position.z - actorPosition.z};
    int yaw = creature->m_data->GetDir().Yaw;
    if (!aim.IsZero()) {
        yaw = static_cast<int>(std::atan2(aim.y, aim.x) * 65535.0 / 6.283185307179586);
    }
    const std::uint16_t targetYaw =
        static_cast<std::uint16_t>(yaw + static_cast<int>((static_cast<double>(damageRange.direction) / 360.0) * 65535.0));
    const double radians = static_cast<double>(targetYaw) * 3.141592653589793 / 32767.5;
    const double directionX = std::cos(radians);
    const double directionY = std::sin(radians);
    const double halfWidth = static_cast<double>(damageRange.angle) * 0.5;
    const double length = static_cast<double>(damageRange.distance);

    FVector leftNear{actorPosition.x - directionY * halfWidth, actorPosition.y + directionX * halfWidth, actorPosition.z};
    FVector rightNear{actorPosition.x + directionY * halfWidth, actorPosition.y - directionX * halfWidth, actorPosition.z};
    FVector leftFar{leftNear.x + directionX * length, leftNear.y + directionY * length, leftNear.z};
    FVector rightFar{rightNear.x + directionX * length, rightNear.y + directionY * length, rightNear.z};
    const int radius = static_cast<int>(std::sqrt(
        (leftFar.x - actorPosition.x) * (leftFar.x - actorPosition.x) +
        (leftFar.y - actorPosition.y) * (leftFar.y - actorPosition.y)
    ));

    CTerritory* const territory = new CTerritory;
    territory->AddTerritoryPoint(&leftNear);
    territory->AddTerritoryPoint(&leftFar);
    territory->AddTerritoryPoint(&rightFar);
    territory->AddTerritoryPoint(&rightNear);

    const int actorZ = runtime::Win64CvttDoubleToInt32(actorPosition.z);
    const int bottomZ =
        affectScopeMinZ == 0 || affectScopeMaxZ == 0 ? actorZ : runtime::AddInt32Wrapping(actorZ, affectScopeMinZ);
    const int topZ = affectScopeMinZ == 0 || affectScopeMaxZ == 0
        ? runtime::AddInt32Wrapping(actorZ, runtime::Win64CvttDoubleToInt32(creature->m_data->m_dCollisionHeight))
        : runtime::AddInt32Wrapping(actorZ, affectScopeMaxZ);

    struct FilterCond {
        FilterCond(
            CCreature* const exceptCreature,
            CCreature* const creature,
            CTerritory* const territory,
            const SkillAffectObjectTypeEnum affectObjectType,
            const int bottomZ,
            const int topZ
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            territory(territory),
            affectObjectType(affectObjectType),
            bottomZ(bottomZ),
            topZ(topZ) {}

        virtual bool CanDo(CCreature* const target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, false)) {
                return false;
            }
            const FVector targetPosition = target->GetPos();
            if (!territory->IsInside(targetPosition, false)) {
                return false;
            }
            const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
            const int targetTop = runtime::AddInt32Wrapping(
                targetZ, runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight)
            );
            return targetZ <= topZ && bottomZ <= targetTop;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        CTerritory* territory;
        SkillAffectObjectTypeEnum affectObjectType;
        int bottomZ;
        int topZ;
    } condition{
        exceptCreature,
        creature,
        territory,
        affectObjectType,
        bottomZ,
        topZ,
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& position, const double radius, FilterCond* const condition) :
            CTLMFilterDist(inZoneId, position, radius), condition(condition) {}

        bool CanDo(CObject* const object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    } filter(static_cast<int>(GetInZoneID()), actorPosition, static_cast<double>(radius), &condition);

    struct GatherData {
        CCreature* creature;
        int rangeTargetType;
        int affectLimit;
        bool affectLimitToPc;
        int* targetCount;
        SkillTargetData* targetData;
    } data{
        creature,
        rangeTargetType,
        affectLimit,
        affectLimitToPc,
        nullptr,
        &targetData,
    };
    int targetCount = 0;
    data.targetCount = &targetCount;

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* const data) : data(data) {}

        bool CanDo(CObject* const object, const double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PushCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->rangeTargetType, data->affectLimit, data->affectLimitToPc,
                    *data->targetCount, *data->targetData, distance, true, FVector{}
                );
            }
            return true;
        }

        GatherData* data;
    } function(&data);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    L2_RELEASE(territory, ObjectRefType::CreateDelete);
}

void CCreature::GatherNeighborChargeTarget(
    const int rangeTargetType,
    const int affectLimit,
    SkillTargetData& targetData,
    const int affectRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    CCreature* exceptCreature
) {
    // Current L2Server 0x0060dc9c.
    L2_GUARD_FUNCTION();
    if (creature->m_ChargeData.IsZero()) {
        // Log.Add(LOG_ERROR, L"ChargeData is zero()")
        // Log.Add(
        //     LOG_ERROR,
        //     "[skills] Charge target collection skipped for object %u: the destination is within the zero-position
        //     tolerance.", creature->m_nId)
        L2_LOG_ERROR(
            Log,
            "[skills] Charge target collection skipped for object %u: the destination is within the zero-position "
            "tolerance.",
            creature->m_nId
        );
        return;
    }

    const FVector actorPosition = creature->GetPos();
    const FVector destination = creature->m_ChargeData.GetTo();
    const FVector direction = creature->m_ChargeData.GetDir();
    CTerritory territory;
    territory.MakeRectangle(actorPosition, destination, direction, static_cast<double>(affectRange));

    const int actorZ = runtime::Win64CvttDoubleToInt32(actorPosition.z);
    const int bottomZ =
        affectScopeMinZ == 0 || affectScopeMaxZ == 0 ? actorZ : runtime::AddInt32Wrapping(actorZ, affectScopeMinZ);
    const int topZ = affectScopeMinZ == 0 || affectScopeMaxZ == 0
        ? runtime::AddInt32Wrapping(actorZ, runtime::Win64CvttDoubleToInt32(creature->m_data->m_dCollisionHeight))
        : runtime::AddInt32Wrapping(actorZ, affectScopeMaxZ);
    const double radius = creature->m_ChargeData.GetDist() * 0.5;
    const FVector center{actorPosition.x + direction.x * radius, actorPosition.y + direction.y * radius,
        actorPosition.z + direction.z * radius};

    struct FilterCond {
        FilterCond(
            CCreature* const exceptCreature,
            CCreature* const creature,
            CTerritory* const territory,
            const SkillAffectObjectTypeEnum affectObjectType,
            const int bottomZ,
            const int topZ
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            territory(territory),
            affectObjectType(affectObjectType),
            bottomZ(bottomZ),
            topZ(topZ) {}

        virtual bool CanDo(CCreature* const target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, false)) {
                return false;
            }

            const FVector targetPosition = target->GetPos();
            if (territory->IsInside(targetPosition, false)) {
                return true;
            }

            // The current body uses the shipped OR structure: a target is
            // rejected only when it is outside both the rectangle and the
            // vertical interval.
            const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
            const int targetTop = runtime::AddInt32Wrapping(
                targetZ, runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight)
            );
            return targetZ <= topZ && bottomZ <= targetTop;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        CTerritory* territory;
        SkillAffectObjectTypeEnum affectObjectType;
        int bottomZ;
        int topZ;
    } condition{
        exceptCreature,
        creature,
        &territory,
        affectObjectType,
        bottomZ,
        topZ,
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& center, const double radius, FilterCond* const condition) :
            CTLMFilterDist(inZoneId, center, radius), condition(condition) {}

        bool CanDo(CObject* const object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    } filter(static_cast<int>(GetInZoneID()), center, radius, &condition);

    struct GatherData {
        CCreature* creature;
        int rangeTargetType;
        int affectLimit;
        int* targetCount;
        SkillTargetData* targetData;
    } data{
        creature,
        rangeTargetType,
        affectLimit,
        nullptr,
        &targetData,
    };
    int targetCount = 0;
    data.targetCount = &targetCount;

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* const data) : data(data) {}

        bool CanDo(CObject* const object, const double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PushCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->rangeTargetType, data->affectLimit, false,
                    *data->targetCount, *data->targetData, distance, true, FVector{}
                );
            }
            return true;
        }

        GatherData* data;
    } function(&data);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborCreatureRingRange(
    const FVector position,
    const int rangeTargetType,
    const int affectLimit,
    SkillTargetData& targetData,
    const int affectRange,
    const DamageRangeType& damageRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    CCreature* exceptCreature,
    const bool checkCanSee,
    const bool unknown
) {
    // Current L2Server 0x00613f7c.
    L2_GUARD_FUNCTION();
    const bool checkZ = affectScopeMinZ != 0 && affectScopeMaxZ != 0;
    const int positionZ = runtime::Win64CvttDoubleToInt32(position.z);
    const int bottomZ = checkZ ? runtime::AddInt32Wrapping(positionZ, affectScopeMinZ) : 0;
    const int topZ = checkZ ? runtime::AddInt32Wrapping(positionZ, affectScopeMaxZ) : 0;

    struct FilterCond {
        FilterCond(
            CCreature* exceptCreature,
            CCreature* creature,
            const SkillAffectObjectTypeEnum affectObjectType,
            const bool unknown,
            const bool checkZ,
            const int bottomZ,
            const int topZ
        ) :
            exceptCreature(exceptCreature),
            creature(creature),
            affectObjectType(affectObjectType),
            unknown(unknown),
            checkZ(checkZ),
            bottomZ(bottomZ),
            topZ(topZ) {}

        virtual bool CanDo(CCreature* target) {
            if (target == nullptr || target == exceptCreature || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, unknown)) {
                return false;
            }
            if (checkZ) {
                const FVector targetPosition = target->GetPos();
                const int targetZ = runtime::Win64CvttDoubleToInt32(targetPosition.z);
                const int targetTop = runtime::AddInt32Wrapping(
                    targetZ, runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight)
                );
                if (topZ < targetZ || targetTop < bottomZ) {
                    return false;
                }
            }
            return true;
        }

        virtual ~FilterCond() {
            exceptCreature = nullptr;
            creature = nullptr;
        }

        CCreature* exceptCreature;
        CCreature* creature;
        SkillAffectObjectTypeEnum affectObjectType;
        bool unknown;
        bool checkZ;
        int bottomZ;
        int topZ;
    };

    struct GatherData {
        CCreature* creature;
        int rangeTargetType;
        int affectLimit;
        int* targetCount;
        SkillTargetData* targetData;
        bool checkCanSee;
        const FVector* position;
    };

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(GatherData* data) : data(data) {}

        bool CanDo(CObject* object, const double distance) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                CCreature::PopCreatureAfterCheckRangeAndCanSee(
                    data->creature, object->CastCreature(), data->rangeTargetType, data->affectLimit, *data->targetCount,
                    *data->targetData, distance, data->checkCanSee, *data->position
                );
            }
            return true;
        }

        GatherData* data;
    };


    SkillTargetData innerTargets;
    int targetCount = 0;
    {
        FilterCond condition(exceptCreature, creature, affectObjectType, unknown, checkZ, bottomZ, topZ);

        struct TLMFilter final : CTLMFilterDist {
            TLMFilter(const int inZoneId, const FVector& position, const double radius, FilterCond* condition) :
                CTLMFilterDist(inZoneId, position, radius), condition(condition) {}

            bool CanDo(CObject* object) override {
                return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
            }

            FilterCond* condition;
        } filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(damageRange.distance), &condition);

        GatherData data{
            creature,
            rangeTargetType,
            affectLimit,
            &targetCount,
            &innerTargets,
            checkCanSee,
            &position,
        };
        TLMFunc function(&data);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    }

    struct FilterCond2 final : FilterCond {
        FilterCond2(
            CCreature* exceptCreature,
            CCreature* creature,
            const SkillAffectObjectTypeEnum affectObjectType,
            const bool unknown,
            const bool checkZ,
            const int bottomZ,
            const int topZ,
            SkillTargetData* excluded
        ) :
            FilterCond(exceptCreature, creature, affectObjectType, unknown, checkZ, bottomZ, topZ), excluded(excluded) {}

        bool CanDo(CCreature* target) override {
            return FilterCond::CanDo(target) && target != nullptr && CreatureCommonFunc::IsNotDuplicated(target, *excluded);
        }

        SkillTargetData* excluded;
    };

    FilterCond2 condition(exceptCreature, creature, affectObjectType, unknown, checkZ, bottomZ, topZ, &innerTargets);

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& position, const double radius, FilterCond2* condition) :
            CTLMFilterDist(inZoneId, position, radius), condition(condition) {}

        bool CanDo(CObject* object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond2* condition;
    } filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(affectRange), &condition);

    GatherData data{
        creature,
        rangeTargetType,
        affectLimit,
        &targetCount,
        &targetData,
        checkCanSee,
        &position,
    };
    TLMFunc function(&data);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborMPCC(
    SkillTargetData& targetData, const int affectRange, CCreature* creature, const bool deadOnly
) {
    // Current L2Server 0x006106d4.
    L2_GUARD_FUNCTION();
    if (!IsPC()) {
        return;
    }
    User* const owner = GetUserOrMaster();
    if (owner == nullptr) {
        return;
    }

    L2_GUARD(*m_lock);
    const double squaredRange = static_cast<double>(runtime::MultiplyInt32Wrapping(affectRange, affectRange));
    const FVector ownerPosition = owner->GetPos();

    const auto addTarget = [&](CCreature* target, double distance) {
        if (target != nullptr && CreatureCommonFunc::IsNotDuplicated(target, targetData)) {
            targetData.c.push_back(CSafePointer2<CCreature>(target));
            targetData.d.push_back(distance);
        }
    };
    CMultiPartyCommandChannel* const channel = owner->GetMPCC();
    if (channel == nullptr) {
        if (!CreatureCommonFunc::IsNotDuplicated(owner, targetData)) {
            return;
        }
        if (owner->GetInZoneID() == creature->GetInZoneID()) {
            targetData.c.push_back(CSafePointer2<CCreature>(owner));
            targetData.d.push_back(0.0);
        }
        if (!deadOnly) {
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, {
                if (!summon->IsSummonOrPet() || !summon->IsAlive() || summon->IsGMInvisible() ||
                    summon->GetInZoneID() != creature->GetInZoneID()) {
                    continue;
                }
                const double squaredDistance = summon->GetPos().DistCube(ownerPosition);
                if (affectRange == -2 || squaredDistance < squaredRange) {
                    addTarget(summon, squaredDistance);
                }
            });
        }
        return;
    }

    std::vector<User*> members;
    channel->GetAllMember(members);
    for (User* const member : members) {
        if (member == nullptr || member->m_data->m_nPrivateID != creature->m_data->m_nPrivateID ||
            member->GetInZoneID() != creature->GetInZoneID()) {
            continue;
        }

        const bool memberAlive = member->IsAlive();
        const bool matchesAliveState = deadOnly ? !memberAlive : memberAlive;
        if (matchesAliveState && !member->IsGMInvisible()) {
            const double squaredDistance = member->GetPos().DistCube(ownerPosition);
            if (affectRange == -2 || squaredDistance < squaredRange) {
                addTarget(member, squaredDistance);
            }
        }
        // Current still lists and emits in_code frames when deadOnly is
        // true in the channel branch, but the action condition then rejects.
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(member, summon, {
            if (deadOnly || !summon->IsSummonOrPet() || !summon->IsAlive() || summon->IsGMInvisible() ||
                summon->GetInZoneID() != creature->GetInZoneID()) {
                continue;
            }
            const double squaredDistance = summon->GetPos().DistCube(ownerPosition);
            if (affectRange == -2 || squaredDistance < squaredRange) {
                addTarget(summon, squaredDistance);
            }
        });
    }
}

void CCreature::GatherNeighborParty(
    SkillTargetData& targetData, int affectRange, int affectScopeMinZ, int affectScopeMaxZ, CCreature* creature, bool unknown
) {
    L2_GUARD_FUNCTION();

    User* owner = nullptr;
    if (IsSummonOrPet()) {
        CSummon* const summon = CastSummon();
        owner = summon->GetMaster();
    } else if (!IsPC()) {
        targetData.c.push_back(CSafePointer2<CCreature>(this));
        targetData.d.push_back(0.0);
        return;
    } else {
        owner = CastUser();
    }

    bool checkZ = false;
    if (affectScopeMinZ != 0 && affectScopeMaxZ != 0) {
        (void)GetPos();
        (void)GetPos();
        checkZ = true;
    }
    if (owner == nullptr) {
        return;
    }

    L2_GUARD(*m_lock);
    const double squaredRange = static_cast<double>(runtime::MultiplyInt32Wrapping(affectRange, affectRange));
    CParty* const party = owner->GetParty();
    if (party != nullptr) {
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (creature->m_data->m_nPrivateID != member->m_data->m_nPrivateID) {
                continue;
            }
            if (member->GetInZoneID() != creature->GetInZoneID()) {
                continue;
            }

            bool matchesAliveState = member->IsAlive();
            if (unknown) {
                matchesAliveState = !matchesAliveState;
            }
            if (matchesAliveState && !member->IsGMInvisible()) {
                const FVector sourcePosition = IsSummonOrPet() ? GetPos() : owner->GetPos();
                const FVector memberPosition = member->GetPos();
                const double squaredDistance = memberPosition.DistCube(sourcePosition);
                if (squaredDistance < squaredRange || affectRange == -2) {
                    bool inZRange = true;
                    if (checkZ) {
                        const FVector position = member->m_data->GetPos();
                        const int z = runtime::Win64CvttDoubleToInt32(position.z);
                        const int height = runtime::Win64CvttDoubleToInt32(member->m_data->m_dCollisionHeight);
                        inZRange = affectScopeMaxZ >= z && runtime::AddInt32Wrapping(height, z) >= affectScopeMinZ;
                    }
                    if (inZRange && CreatureCommonFunc::IsNotDuplicated(member, targetData)) {
                        targetData.c.push_back(CSafePointer2<CCreature>(member));
                        targetData.d.push_back(squaredDistance);
                    }
                }
            }

            if (!unknown) {
                FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(member, summon, {
                    if (!summon->IsAlive() || summon->IsGMInvisible()) {
                        continue;
                    }
                    if (summon->GetInZoneID() != creature->GetInZoneID()) {
                        continue;
                    }

                    const FVector sourcePosition = owner->GetPos();
                    const FVector summonPosition = summon->GetPos();
                    const double squaredDistance = summonPosition.DistCube(sourcePosition);
                    if (!(squaredDistance < squaredRange || affectRange == -2)) {
                        continue;
                    }

                    if (checkZ) {
                        const FVector position = summon->m_data->GetPos();
                        const int z = runtime::Win64CvttDoubleToInt32(position.z);
                        const int height = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dCollisionHeight);
                        if (affectScopeMaxZ < z || runtime::AddInt32Wrapping(height, z) < affectScopeMinZ) {
                            continue;
                        }
                    }
                    if (CreatureCommonFunc::IsNotDuplicated(summon, targetData)) {
                        targetData.c.push_back(CSafePointer2<CCreature>(summon));
                        targetData.d.push_back(squaredDistance);
                    }
                });
            }
        }
        return;
    }

    if (!CreatureCommonFunc::IsNotDuplicated(owner, targetData)) {
        return;
    }

    if (owner->GetInZoneID() == creature->GetInZoneID()) {
        bool inZRange = true;
        if (checkZ) {
            const FVector position = owner->m_data->GetPos();
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int height = runtime::Win64CvttDoubleToInt32(owner->m_data->m_dCollisionHeight);
            inZRange = affectScopeMaxZ >= z && runtime::AddInt32Wrapping(height, z) >= affectScopeMinZ;
        }
        if (inZRange) {
            targetData.c.push_back(CSafePointer2<CCreature>(owner));
            targetData.d.push_back(0.0);
        }
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, {
        if (!summon->IsAlive() || summon->IsGMInvisible()) {
            continue;
        }
        if (summon->GetInZoneID() != creature->GetInZoneID()) {
            continue;
        }

        const FVector sourcePosition = owner->GetPos();
        const FVector summonPosition = summon->GetPos();
        const double squaredDistance = summonPosition.DistCube(sourcePosition);
        if (!(squaredDistance < squaredRange || affectRange == -2)) {
            continue;
        }

        if (checkZ) {
            const FVector position = summon->m_data->GetPos();
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int height = runtime::Win64CvttDoubleToInt32(summon->m_data->m_dCollisionHeight);
            if (affectScopeMaxZ < z || runtime::AddInt32Wrapping(height, z) < affectScopeMinZ) {
                continue;
            }
        }
        if (CreatureCommonFunc::IsNotDuplicated(summon, targetData)) {
            targetData.c.push_back(CSafePointer2<CCreature>(summon));
            targetData.d.push_back(squaredDistance);
        }
    });
}

void CCreature::GatherNeighborPledge(
    SkillTargetData& targetData, int affectRange, int affectScopeMinZ, int affectScopeMaxZ, CCreature* creature, bool unknown
) {
    L2_GUARD_FUNCTION();

    if (!IsPCOrSummon()) {
        targetData.c.push_back(CSafePointer2<CCreature>(this));
        targetData.d.push_back(0.0);
        return;
    }

    User* const owner = GetUserOrMaster();
    if (owner == nullptr) {
        return;
    }

    const FVector position = m_data->m_Pos;
    bool checkZ = false;
    int bottomZ = 0;
    int topZ = 0;
    if (affectScopeMinZ != 0 && affectScopeMaxZ != 0) {
        const int positionZ = runtime::Win64CvttDoubleToInt32(position.z);
        bottomZ = runtime::AddInt32Wrapping(positionZ, affectScopeMinZ);
        topZ = runtime::AddInt32Wrapping(positionZ, affectScopeMaxZ);
        checkZ = true;
    }

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(
            int inZoneId,
            const FVector& position,
            double distance,
            User* owner,
            CCreature* creature,
            bool unknown,
            bool checkZ,
            int bottomZ,
            int topZ
        ) :
            CTLMFilterDist(inZoneId, position, distance),
            owner(owner),
            creature(creature),
            unknown(unknown),
            checkZ(checkZ),
            bottomZ(bottomZ),
            topZ(topZ) {}

        bool CanDo(CObject* object) override {
            if (!CTLMFilterDist::CanDo(object)) {
                return false;
            }

            User* target = nullptr;
            if (object->IsPC()) {
                target = object->CastUser();
            }
            if (target == nullptr) {
                return false;
            }

            if (target != owner) {
                if (target->m_data->m_nPledgeID == 0 || target->m_data->m_nPledgeID != owner->m_data->m_nPledgeID ||
                    target->IsOlympiadPlayer() || target->IsOlympiadObserver() || target->m_PledgeGameUser.IsPlayer() ||
                    target->m_PledgeGameUser.IsObserver()) {
                    return false;
                }
            }

            if (creature->m_data->m_nPrivateID != target->m_data->m_nPrivateID) {
                return false;
            }

            bool matchesAliveState = target->IsAlive();
            if (unknown) {
                matchesAliveState = !matchesAliveState;
            }
            if (!matchesAliveState) {
                return false;
            }

            if (checkZ) {
                const int targetZ = runtime::Win64CvttDoubleToInt32(target->m_data->m_Pos.z);
                if (topZ < targetZ) {
                    return false;
                }
                const int collisionHeight = runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight);
                if (runtime::AddInt32Wrapping(collisionHeight, targetZ) < bottomZ) {
                    return false;
                }
            }
            return true;
        }

        User* owner;
        CCreature* creature;
        bool unknown;
        bool checkZ;
        int bottomZ;
        int topZ;
    };

    struct TLMFunc final : CTLMFunc {
        TLMFunc(SkillTargetData* targetData, const FVector* position) : targetData(targetData), position(position) {}

        bool CanDo(CObject* object, double distance) override {
            User* const target = object->CastUser();
            if (target == nullptr || (!object->IsDeleting() && object->m_Deleted == 0)) {
                if (CreatureCommonFunc::IsNotDuplicated(target, *targetData)) {
                    targetData->c.push_back(CSafePointer2<CCreature>(target));
                    targetData->d.push_back(distance);
                }

                FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(target, summon, {
                    if (CreatureCommonFunc::IsNotDuplicated(summon, *targetData)) {
                        targetData->c.push_back(CSafePointer2<CCreature>(summon));
                        const FVector summonPosition = summon->GetPos();
                        targetData->d.push_back(summonPosition.DistCube(*position));
                    }
                });
            }
            return true;
        }

        SkillTargetData* targetData;
        const FVector* position;
    };


    TLMFilter filter(
        static_cast<int>(GetInZoneID()), position, static_cast<double>(affectRange), owner, creature, unknown, checkZ,
        bottomZ, topZ
    );
    TLMFunc function(&targetData, &position);
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
}

void CCreature::GatherNeighborAirShip(
    const FVector position, CCreature* creature, CSkillInfo* skillInfo, const bool checkCanSee
) {
    // Current L2Server 0x005ed638.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || skillInfo == nullptr) {
        return;
    }
    CAirShip* const ownAirShip = creature->GetMyAirShip();
    if (ownAirShip == nullptr) {
        return;
    }

    const int positionZ = runtime::Win64CvttDoubleToInt32(position.z);
    const int bottomZ = runtime::AddInt32Wrapping(positionZ, skillInfo->m_nAffectScopeMinZ);
    const int topZ = runtime::AddInt32Wrapping(positionZ, skillInfo->m_nAffectScopeMaxZ);
    const int range = skillInfo->m_nAffectRange;
    const double squaredRange = static_cast<double>(runtime::MultiplyInt32Wrapping(range, range));

    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
    BROADCAST_SET_SIZE_FAST(position, range, minX, maxX, minY, maxY, minZ, maxZ);
    FVector outputPosition = position;
    g_ptlAirShipMap->Read();
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            const int lastZ = g_ptlAirShipMap->IsZDivided(x, y) ? maxZ : minZ;
            for (int z = minZ; z <= lastZ; ++z) {
                TLMIterator iterator =
                    g_ptlAirShipMap->GetIterator(static_cast<short>(x), static_cast<short>(y), static_cast<char>(z));
                while (CAirShip* const airShip = __SafeCastAirShip(*iterator)) {
                    if (airShip != ownAirShip && airShip->GetInZoneID() == creature->GetInZoneID()) {
                        const FVector airShipPosition = airShip->GetPos();
                        const int airShipZ = runtime::Win64CvttDoubleToInt32(airShipPosition.z);
                        if (airShipPosition.DistCube(position) <= squaredRange && airShipZ <= topZ &&
                            runtime::AddInt32Wrapping(airShipZ, 750) >= bottomZ) {
                            bool visible = true;
                            if (checkCanSee && g_CheckCollisionForRangeSkill) {
                                visible = CWorld::Inst().GetGeo()->CanSee(
                                    position, airShipPosition, static_cast<int>(creature->GetInZoneID()), true
                                );
                            }
                            if (visible) {
                                airShip->FillOutputVectorWithParts(
                                    creature->m_vSkillStaticTarget, creature->m_vSkillStaticTargetDistance, outputPosition
                                );
                            }
                        }
                    }
                    ++iterator;
                }
            }
        }
    }
}

void CCreature::GatherNeighborCreatureByPoint(
    const int rangeTargetType,
    SkillTargetData& targetData,
    const int affectRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature* creature,
    const FVector position
) {
    // Current L2Server 0x005fb374.
    L2_GUARD_FUNCTION();

    const FVector ownPosition = m_data->m_Pos;
    const bool checkZ = affectScopeMinZ != 0 && affectScopeMaxZ != 0;
    const int bottomZ = checkZ ? runtime::Win64CvttDoubleToInt32(ownPosition.z) + affectScopeMinZ : 0;
    const int topZ = checkZ ? runtime::Win64CvttDoubleToInt32(ownPosition.z) + affectScopeMaxZ : 0;

    struct FilterCond {
        FilterCond(
            CCreature* const creature,
            const SkillAffectObjectTypeEnum affectObjectType,
            const bool checkZ,
            const int bottomZ,
            const int topZ,
            const int rangeTargetType
        ) :
            creature(creature),
            affectObjectType(affectObjectType),
            checkZ(checkZ),
            bottomZ(bottomZ),
            topZ(topZ),
            rangeTargetType(rangeTargetType) {}

        bool CanDo(CCreature* const target) {
            if (target == nullptr || target == creature ||
                !CCreature::CheckRelationWithCaster(creature, target, affectObjectType, false)) {
                return false;
            }
            if (checkZ) {
                const int z = runtime::Win64CvttDoubleToInt32(target->m_data->m_Pos.z);
                if (topZ < z || z + runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight) < bottomZ) {
                    return false;
                }
            }
            if (rangeTargetType == RTT_PEACE_ZONE && target->IsPCOrSummon()) {
                return false;
            }
            if (g_CheckCollisionForRangeSkill && !CWorld::Inst().GetGeo()->CanSee(creature, target, true)) {
                return false;
            }
            return true;
        }

        CCreature* creature;
        SkillAffectObjectTypeEnum affectObjectType;
        bool checkZ;
        int bottomZ;
        int topZ;
        int rangeTargetType;
    } condition{
        creature,
        affectObjectType,
        checkZ,
        bottomZ,
        topZ,
        rangeTargetType,
    };

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(const int inZoneId, const FVector& position, const double range, FilterCond* const condition) :
            CTLMFilterDist(inZoneId, position, range), condition(condition) {}

        bool CanDo(CObject* const object) override {
            return CTLMFilterDist::CanDo(object) && condition->CanDo(object->CastCreature());
        }

        FilterCond* condition;
    } filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(affectRange), &condition);

    struct TLMFunc final : CTLMFunc {
        explicit TLMFunc(SkillTargetData* const targetData) : targetData(targetData) {}

        bool CanDo(CObject* const object, double) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                targetData->c.push_back(CSafePointer2<CCreature>(object->CastCreature()));
                targetData->d.push_back(-1.5);
            }
            return true;
        }

        SkillTargetData* targetData;
    } function(&targetData);


    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
}

void CCreature::GatherNeighborStaticObject(
    const FVector position,
    std::vector<CSafePointer2<CStaticObject>>& targets,
    std::vector<double>& distances,
    const int affectRange,
    const int affectScopeMinZ,
    const int affectScopeMaxZ,
    const SkillAffectObjectTypeEnum affectObjectType,
    CCreature*
) {
    // Current L2Server 0x005f5908.
    L2_GUARD_FUNCTION();
    if (affectObjectType != SAFOT_HIDDEN_PLACE) {
        return;
    }

    const bool checkZ = affectScopeMinZ != 0 && affectScopeMaxZ != 0;
    const int bottomZ = checkZ ? runtime::Win64CvttDoubleToInt32(position.z) + affectScopeMinZ : 0;
    const int topZ = checkZ ? runtime::Win64CvttDoubleToInt32(position.z) + affectScopeMaxZ : 0;

    struct TLMFilter final : CTLMFilterDist {
        TLMFilter(
            const int inZoneId,
            const FVector& position,
            const double range,
            const bool checkZ,
            const int bottomZ,
            const int topZ
        ) :
            CTLMFilterDist(inZoneId, position, range), checkZ(checkZ), bottomZ(bottomZ), topZ(topZ) {}

        bool CanDo(CObject* object) override {
            if (!CTLMFilterDist::CanDo(object)) {
                return false;
            }
            CStaticObject* const staticObject = object->CastStaticObject();
            if (staticObject == nullptr || staticObject->m_data == nullptr) {
                return false;
            }
            if (checkZ) {
                const FVector targetPosition = staticObject->GetPos();
                const int z = runtime::Win64CvttDoubleToInt32(targetPosition.z);
                if (topZ < z || z + runtime::Win64CvttDoubleToInt32(staticObject->GetCollisionHeight()) < bottomZ) {
                    return false;
                }
            }
            return staticObject->m_data->m_nStealth == 0;
        }

        bool checkZ;
        int bottomZ;
        int topZ;
    };

    struct TLMFunc final : CTLMFunc {
        TLMFunc(std::vector<CSafePointer2<CStaticObject>>& targets, std::vector<double>& distances) :
            targets(targets), distances(distances) {}

        bool CanDo(CObject* object, const double distance) override {
            CStaticObject* const staticObject = object->CastStaticObject();
            CSafePointer2<CStaticObject> target(staticObject);
            targets.push_back(target);
            distances.push_back(distance);
            return true;
        }

        std::vector<CSafePointer2<CStaticObject>>& targets;
        std::vector<double>& distances;
    };


    TLMFilter filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(affectRange), checkZ, bottomZ, topZ);
    TLMFunc function(targets, distances);
    FOR_ALL_NEIGHBOR_STATIC_OBJECT_DIST(&filter, &function);
}

void CCreature::GatherAllSummonAndMaster(SkillTargetData& targetData, const bool exceptMaster) {
    // Current L2Server 0x0061133c.
    L2_GUARD_FUNCTION();
    User* const master = GetUserOrMaster();
    if (master == nullptr) {
        return;
    }
    if (!exceptMaster) {
        targetData.c.push_back(CSafePointer2<CCreature>(master));
        targetData.d.push_back(0.0);
    }
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(master, summon, {
        targetData.c.push_back(CSafePointer2<CCreature>(summon));
        targetData.d.push_back(0.0);
    });
}

void CCreature::_ChangeAbnormalStatus(const int index, CCreature*, const CSkillInfo*) {
    L2_GUARD_FUNCTION();
    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    const std::uint64_t infectedSkillCount = m_InfectedSkills.size();
    if (static_cast<std::uint64_t>(static_cast<std::int64_t>(index)) < infectedSkillCount) {
        runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(index)).nStartTime = now;
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"Try to change abnormal status pass-by-index. [%d] [%d]",
    //     static_cast<int>(infectedSkillCount),
    //     index)
    // Log.Add(
    //     LOG_ERROR,
    //     "[skills] Cannot refresh an abnormal-effect start time for object %u: index %d is outside the list of %llu
    //     entries.", m_nId, index, static_cast<unsigned long long>(infectedSkillCount))
    L2_LOG_ERROR(
        Log,
        "[skills] Cannot refresh an abnormal-effect start time for object %u: index %d is outside the list of %llu entries.",
        m_nId, index, static_cast<unsigned long long>(infectedSkillCount)
    );
}

bool CCreature::GetItem(CItem* item, bool force) {
    L2_GUARD_FUNCTION();
    return CWorld::Inst().TryPickItem(item, this, force);
}

// Current L2Server.exe 0x005ec8ac.
bool CCreature::PickItem(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr || !item->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[] is not exist.",
        //     L"Creature_server.cpp",
        //     0x3475)
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Pickup rejected for object %u: the supplied object is %s.",
        //     m_nId,
        //     item != nullptr ? "not an item" : "null")
        L2_LOG_ERROR(
            Log, "[inventory] Pickup rejected for object %u: the supplied object is %s.", m_nId,
            item != nullptr ? "not an item" : "null"
        );
        return false;
    }

    User* const user = CastUser();
    if (user->m_inv.CheckAddable(
            // Current CCreature::PickItem@0x005ec8ac passes
            // CSharedItemData::index (+0x28) to the two-argument,
            // runtime-item overload. Passing classID (+0x58) makes the
            // item factory lookup fail before the CacheD pickup request.
            item->m_data->index, false
        )) {
        db.RequestPickItem(user, item);
        return true;
    }

    SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
    return false;
}

bool CCreature::GetItemReuseDelay(int itemId, std::uint32_t* reuseEndTick) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedItemsSect);
    const auto position = m_UsedItemsLastTimeMap.find(itemId);
    if (position == m_UsedItemsLastTimeMap.end()) {
        return false;
    }

    *reuseEndTick = position->second;
    return true;
}

std::uint32_t CCreature::GetItemRemindTimeMs(int itemId) {
    L2_GUARD_FUNCTION();
    std::uint32_t reuseEndTick = 0;
    if (!GetItemReuseDelay(itemId, &reuseEndTick)) {
        return 0;
    }

    const std::uint32_t remaining = reuseEndTick - runtime::GetTickCount();
    std::int32_t signedRemaining = 0;
    static_assert(sizeof(signedRemaining) == sizeof(remaining));
    std::memcpy(&signedRemaining, &remaining, sizeof(signedRemaining));
    return signedRemaining > 0 ? remaining : 0;
}

// Current L2Server.exe 0x0060a850.
void CCreature::SetItemReuseDelay(int itemId, std::uint32_t reuseDelay) {
    L2_GUARD_FUNCTION();
    if (reuseDelay == UINT32_MAX) {
        const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
        const std::int32_t hour = static_cast<std::int32_t>(localTime.wHour);
        const std::int32_t boundarySeconds = hour < 6 || (hour == 6 && localTime.wMinute < 30) ? 0x5b72 : 0x1acf2;
        const std::int32_t secondsUntilBoundary = boundarySeconds -
            (hour * 60 + static_cast<std::int32_t>(localTime.wMinute)) * 60 - static_cast<std::int32_t>(localTime.wSecond);
        reuseDelay = static_cast<std::uint32_t>(secondsUntilBoundary * 1000);
    }

    std::uint32_t reuseEndTick = 0;
    bool changed = false;
    {
        L2_GUARD(m_UsedItemsSect);
        const auto position = m_UsedItemsLastTimeMap.find(itemId);
        if (position == m_UsedItemsLastTimeMap.end()) {
            ::CIOObject::SetShortTimeStamp(&reuseEndTick, reuseDelay);
            m_UsedItemsLastTimeMap.emplace(itemId, reuseEndTick);
            changed = true;
        } else {
            changed = ::CIOObject::SetShortTimeStamp(&position->second, reuseDelay);
            if (changed) {
                reuseEndTick = position->second;
            }
        }
    }

    if (changed && IsPC()) {
        const std::uint32_t currentTick = runtime::GetTickCount();
        const std::int32_t currentTime = runtime::crt::Time32Now();
        const std::int64_t remainingMilliseconds =
            static_cast<std::int64_t>(reuseEndTick) - static_cast<std::int64_t>(currentTick);
        const std::int32_t reuseEndTime =
            runtime::AddInt32Wrapping(currentTime, static_cast<std::int32_t>(remainingMilliseconds / 1000));
        User* const user = CastUser();
        db.RequestSetItemReuseDelay(user, itemId, reuseEndTime);
    }
}

void CCreature::InsertItemReuseDelay(int itemId, std::uint32_t reuseDelay) {
    L2_GUARD_FUNCTION();
    std::uint32_t reuseEndTick = 0;
    {
        L2_GUARD(m_UsedItemsSect);
        ::CIOObject::SetShortTimeStamp(&reuseEndTick, reuseDelay);
        m_UsedItemsLastTimeMap[itemId] = reuseEndTick;
    }
}

bool CCreature::ValidateItemSkills() {
    L2_GUARD_FUNCTION();
    return false;
}

void CCreature::InitMes(MesType::Enum type) {
    L2_GUARD_FUNCTION();
    const int index = static_cast<int>(type);
    m_data->m_MesTime[index] = 0;
    m_data->m_MesCount[index] = 0;
    m_data->m_InfectedMes[index] = false;
}

void CCreature::SetInstantZoneTypeID(int instantZoneTypeId) {
    L2_GUARD_FUNCTION();
    m_nInstantZoneTypeID = instantZoneTypeId;
    m_data->m_nInstantZoneTypeID = static_cast<std::uint32_t>(instantZoneTypeId);
}

bool CCreature::ValidateExtraParam() {
    L2_GUARD_FUNCTION();
    ValidateEnchantExtraBonus();
    return true;
}

int CCreature::MesCount(MesType::Enum type) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MesType::MAX)) {
        return 0;
    }

    L2_GUARD(*m_lock);
    const int count = m_data->m_MesCount[static_cast<int>(type)];
    return count;
}

void CCreature::InfectedMes(MesType::Enum type, bool infected) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MesType::MAX)) {
        return;
    }

    L2_GUARD(*m_lock);
    m_data->m_InfectedMes[static_cast<int>(type)] = infected;
}

void CCreature::InitMes() {
    L2_GUARD_FUNCTION();
    for (int type = 0; type < MesType::MAX; ++type) {
        InitMes(static_cast<MesType::Enum>(type));
    }
}

void CCreature::MesCheck() {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (int type = 0; type < MesType::MAX; ++type) {
        if (m_data->m_InfectedMes[type]) {
            m_data->m_MesTime[type] = m_nTickCount;
            m_data->m_MesCount[type] = runtime::AddInt32Wrapping(m_data->m_MesCount[type], 1);
            m_data->m_InfectedMes[type] = false;
        }
        if (m_data->m_MesCount[type] != 0 &&
            m_data->m_MesTime[type] < runtime::SubtractInt32Wrapping(m_nTickCount, 0x16)) {
            InitMes(static_cast<MesType::Enum>(type));
        }
    }
}

bool CCreature::IsSeeEvent(CCreature* actor, CCreature* target) {
    if (actor->m_AvoidAgroWhenEnterWorld || target->m_AvoidAgroWhenEnterWorld) {
        return false;
    }
    return actor->m_data->m_nExCrtEffect == 1 || target->m_data->m_nExCrtEffect == 1 ||
        (actor->m_SkillMod.m_bSeeEvent && target->m_SkillMod.m_bSeeEvent);
}

bool CCreature::_NeedLookNeightbor(CCreature& target, double distanceSquared, int range) {
    L2_GUARD_FUNCTION();

    if (!target.IsVisible() || target.IsGMInvisible() || target.m_data->m_bIsAlive == 0 ||
        g_lookNeighborNpcIdCount >= static_cast<std::int32_t>(kMaxLookNeighborNpcIds)) {
        return false;
    }

    CSharedCreatureData* const eventData = IsPCOrSummon() ? target.m_data : m_data;
    if (!eventData->m_bEventFlag[0]) {
        return false;
    }

    const int maximumAgroRange = std::max(m_data->m_nAgroRange, target.m_data->m_nAgroRange);
    const double maximumAgroRangeSquared =
        static_cast<double>(runtime::MultiplyInt32Wrapping(maximumAgroRange, maximumAgroRange));

    if ((target.IsBoss() || IsBoss()) && distanceSquared <= maximumAgroRangeSquared) {
        return true;
    }

    if (IsTrap()) {
        return false;
    }
    if (distanceSquared > maximumAgroRangeSquared) {
        return false;
    }
    if (range != -1) {
        const double rangeSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(range, range));
        if (distanceSquared >= rangeSquared) {
            return false;
        }
    }
    return true;
}

bool CCreature::_ForAllNeighborInLookNeighbor(CCreature* target, const FVector& previousPosition) {
    L2_GUARD_FUNCTION();

    if (target->m_nId == m_nId) {
        return false;
    }

    int verticalDistance = runtime::Win64CvttDoubleToInt32(m_data->m_Pos.z - target->m_data->m_Pos.z);
    if (verticalDistance < 0) {
        verticalDistance = runtime::SubtractInt32Wrapping(0, verticalDistance);
    }
    const int verticalLimit = static_cast<std::int32_t>(target->m_data->m_moveType) == CMT_FLY ? 3000 : 500;
    if (verticalDistance > verticalLimit) {
        return false;
    }

    const double deltaX = target->m_data->m_Pos.x - m_data->m_Pos.x;
    const double deltaY = target->m_data->m_Pos.y - m_data->m_Pos.y;
    const double deltaZ = target->m_data->m_Pos.z - m_data->m_Pos.z;
    const double distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

    if (!_NeedLookNeightbor(*target, distanceSquared, -1)) {
        if (distanceSquared > 360000.0) {
            m_NeighborCreatureSet.erase(target->m_data->index);
        }
    } else if (m_NeighborCreatureSet.find(target->m_data->index) == m_NeighborCreatureSet.end()) {
        if (!target->IsBoss() && !IsBoss()) {
            m_NeighborCreatureSet.insert(target->m_data->index);
        }

        if (IsSeeEvent(this, target)) {
            const bool actorIsUser = m_data->m_bIsUser;
            const bool targetIsUser = target->m_data->m_bIsUser;
            bool sendNpcEvent = actorIsUser != targetIsUser;
            if (!actorIsUser && !targetIsUser) {
                sendNpcEvent = target->m_data->m_SummonType != NST_NOTHING || m_data->m_SummonType != NST_NOTHING ||
                    target->m_data->m_Race == RT_FRIEND || m_data->m_Race == RT_FRIEND;
            }
            if (sendNpcEvent) {
                g_lookNeighborNpcIds[static_cast<std::size_t>(g_lookNeighborNpcIdCount)] =
                    static_cast<std::int32_t>(target->m_nId);
                ++g_lookNeighborNpcIdCount;
            }
        }
    }

    _CheckTrapInLookNeighbor(target, previousPosition);
    return true;
}

bool CCreature::_ForAllNeighborInLookNeighborByNPC(CCreature* target, const int range, int& npcCount) {
    // Current L2Server 0x00606534.
    L2_GUARD_FUNCTION();

    if (m_data->m_bIsUser) {
        return false;
    }
    if (target->m_nId == m_nId) {
        return false;
    }

    int verticalDistance = runtime::Win64CvttDoubleToInt32(m_data->m_Pos.z - target->m_data->m_Pos.z);
    if (verticalDistance < 0) {
        verticalDistance = runtime::SubtractInt32Wrapping(0, verticalDistance);
    }
    const int verticalLimit = static_cast<std::int32_t>(target->m_data->m_moveType) == CMT_FLY ? 3000 : 500;
    if (verticalDistance > verticalLimit) {
        return false;
    }

    const double deltaX = target->m_data->m_Pos.x - m_data->m_Pos.x;
    const double deltaY = target->m_data->m_Pos.y - m_data->m_Pos.y;
    const double deltaZ = target->m_data->m_Pos.z - m_data->m_Pos.z;
    const double distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

    if (!_NeedLookNeightbor(*target, distanceSquared, range)) {
        if (distanceSquared > 360000.0) {
            m_NeighborCreatureSet.erase(target->m_data->index);
        }
        return true;
    }

    if (m_NeighborCreatureSet.find(target->m_data->index) == m_NeighborCreatureSet.end() && !target->IsBoss() && !IsBoss()) {
        m_NeighborCreatureSet.insert(target->m_data->index);
    }

    if (IsSeeEvent(this, target) && (target->m_data->m_bIsUser || (++npcCount < 6))) {
        g_lookNeighborNpcIds[static_cast<std::size_t>(g_lookNeighborNpcIdCount)] = static_cast<std::int32_t>(target->m_nId);
        ++g_lookNeighborNpcIdCount;
    }
    return true;
}

void CCreature::_CheckTrapInLookNeighbor(CCreature* target, const FVector& previousPosition) {
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return;
    }
    if (IsTrap()) {
        CastTrap()->CheckTrap(*target, CTrap::STEP_IN);
    } else if (target->IsWorldTrap()) {
        target->CastWorldTrap()->CheckTrapByPos(*this, previousPosition, m_data->m_Pos);
    } else if (target->IsPCTrap()) {
        target->CastPCTrap()->CheckTrap(*this, CTrap::STEP_IN);
    }
}

void CCreature::__async_LookNeighbor(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    if (IsRidingWyvern() || IsGMInvisible()) {
        return;
    }
    if (IsNPC() && !CastNPC()->CanGetNpcEvent()) {
        return;
    }
    if (m_AvoidAgroWhenEnterWorld) {
        return;
    }

    FVector previousPosition;
    std::memcpy(static_cast<void*>(&previousPosition), argument->Buffer + 0x08, sizeof(previousPosition));
    g_lookNeighborNpcIdCount = 0;

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(CCreature* actor, const FVector& previousPosition) : m_actor(actor), m_previousPosition(previousPosition) {}

        bool CanDo(CObject* object, double) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                m_actor->_ForAllNeighborInLookNeighbor(object->CastCreature(), m_previousPosition);
            }
            return true;
        }

    private:
        CCreature* m_actor;
        const FVector& m_previousPosition;
    } function(this, previousPosition);

    const FVector position = GetPos();
    CTLMFilterRect filter(static_cast<int>(GetInZoneID()), position, 2048.0);
    FOR_ALL_NEIGHBOR_CREATURE_RECT(&filter, &function);

    if (g_lookNeighborNpcIdCount != 0) {
        g_npcserver.Send(
            "cdbd", 0x11, static_cast<std::int32_t>(m_nId),
            g_lookNeighborNpcIdCount * static_cast<std::int32_t>(sizeof(std::int32_t)), g_lookNeighborNpcIds.data(), -1
        );
    }
}

void CCreature::__async_LookItems(CExecutionArgumentPad* argument) {
    // Current L2Server 0x005f2c78.
    L2_GUARD_FUNCTION();

    if (!IsNPC()) {
        return;
    }

    std::int64_t maxCount = 0;
    std::memcpy(&maxCount, argument->Buffer + 0x10, sizeof(maxCount));
    if (maxCount < 1) {
        return;
    }
    if (maxCount > 0x139c) {
        maxCount = 0x139c;
        std::memcpy(argument->Buffer + 0x10, &maxCount, sizeof(maxCount));
    }

    std::int32_t range = 0;
    std::int32_t itemClassId = 0;
    std::memcpy(&range, argument->Buffer + 0x08, sizeof(range));
    std::memcpy(&itemClassId, argument->Buffer + 0x18, sizeof(itemClassId));

    const int inZoneId = GetInZoneID();
    const FVector ownerPosition = GetPos();
    const bool isBoss = IsBoss();
    const bool checkWorldCollision = IsPC();

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(
            const int maxCount,
            const int itemClassId,
            const int inZoneId,
            const int range,
            const FVector& ownerPosition,
            const bool isBoss,
            const bool checkWorldCollision
        ) :
            m_count(0),
            m_maxCount(maxCount),
            m_itemClassId(itemClassId),
            m_inZoneId(inZoneId),
            m_range(range),
            m_ownerPosition(ownerPosition),
            m_isBoss(isBoss),
            m_checkWorldCollision(checkWorldCollision) {}

        bool CanDo(CObject* object, double) override {
            CItem* const item = object->CastItem();
            CSharedItemData* const data = item->m_data;
            if (!data->m_bIsActive || m_count >= m_maxCount) {
                return true;
            }

            const double deltaX = data->m_Pos.x - m_ownerPosition.x;
            const double deltaY = data->m_Pos.y - m_ownerPosition.y;
            const double deltaZ = data->m_Pos.z - m_ownerPosition.z;
            const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

            std::int32_t verticalDistance = runtime::Win64CvttDoubleToInt32(deltaZ);
            if (verticalDistance < 0) {
                verticalDistance = runtime::SubtractInt32Wrapping(0, verticalDistance);
            }
            if (verticalDistance > 500 || distance >= static_cast<double>(m_range) || (!m_isBoss && distance >= 450.0) ||
                !CWorld::Inst().GetGeo()->CanSee(m_ownerPosition, data->m_Pos, m_inZoneId, m_checkWorldCollision) ||
                (m_itemClassId > 0 && data->m_nClassID != m_itemClassId)) {
                return true;
            }

            m_itemIndices[static_cast<std::size_t>(m_count)] = data->index;
            ++m_count;
            return true;
        }

        std::array<std::int32_t, 0x139c> m_itemIndices;
        std::int32_t m_count;
        std::int32_t m_maxCount;
        std::int32_t m_itemClassId;
        std::int32_t m_inZoneId;
        std::int32_t m_range;
        FVector m_ownerPosition;
        bool m_isBoss;
        bool m_checkWorldCollision;
    } function(
        static_cast<std::int32_t>(maxCount), itemClassId, inZoneId, range, ownerPosition, isBoss, checkWorldCollision
    );


    CTLMFilterRect filter(this, 2048.0);
    FOR_ALL_NEIGHBOR_ITEM_RECT(&filter, &function);

    if (function.m_count != 0) {
        g_npcserver.Send(
            "cddb", 0x15, m_data->index, function.m_count,
            function.m_count * static_cast<std::int32_t>(sizeof(std::int32_t)), function.m_itemIndices.data()
        );
    }
}

void CCreature::__async_LookNeighborByNPC(CExecutionArgumentPad* argument) {
    // Current L2Server 0x005f2e64.
    L2_GUARD_FUNCTION();

    if (IsPCOrSummon()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Only pure NPC use LookNeighborByNPC. class[%d]",
        //     m_data->m_nClassID)
        // Log.Add(
        //     LOG_WARN,
        //     "[creature] NPC-neighbor request rejected for object %u, class %d: the caller is a player or summon.",
        //     m_nId,
        //     m_data->m_nClassID)
        L2_LOG_WARN(
            Log, "[creature] NPC-neighbor request rejected for object %u, class %d: the caller is a player or summon.",
            m_nId, m_data->m_nClassID
        );
        return;
    }
    if (!IsNPC() || !CastNPC()->CanGetNpcEvent()) {
        return;
    }

    g_lookNeighborNpcIdCount = 0;
    int range = 0;
    std::memcpy(&range, argument->Buffer + 0x08, sizeof(range));

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(CCreature* actor, const int range) : m_actor(actor), m_range(range), m_npcCount(0) {}

        bool CanDo(CObject* object, double) override {
            if (!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) {
                m_actor->_ForAllNeighborInLookNeighborByNPC(object->CastCreature(), m_range, m_npcCount);
            }
            return true;
        }

    private:
        CCreature* m_actor;
        int m_range;
        int m_npcCount;
    } function(this, range);

    const FVector position = GetPos();
    CTLMFilterRect filter(static_cast<int>(GetInZoneID()), position, 2048.0);
    FOR_ALL_NEIGHBOR_CREATURE_RECT(&filter, &function);

    if (g_lookNeighborNpcIdCount != 0) {
        if (g_lookNeighborNpcIdCount > 0x100) {
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        g_npcserver.Send(
            "cdbd", 0x11, static_cast<std::int32_t>(m_nId),
            g_lookNeighborNpcIdCount * static_cast<std::int32_t>(sizeof(std::int32_t)), g_lookNeighborNpcIds.data(), -1
        );
    }
}

void CCreature::ResetNeighbor() {
    L2_GUARD_FUNCTION();
    m_nNeighborMapResetCount = 0;
    m_NeighborCreatureSet.clear();
    m_NeighborItemSet.clear();
}

void CCreature::HandleSerializedMessages() {
    L2_GUARD_FUNCTION();
    std::optional<std::vector<CSerializedMessage*>> messagesStorage;
    {
        L2_GUARD(m_SerializerMessageLock);
        if (m_SerializedMessages.empty()) {
            return;
        }

        messagesStorage.emplace(m_SerializedMessages);
        m_SerializedMessages.clear();
    }

    auto& messages = *messagesStorage;
    if (!m_bDeleting && m_Deleted == 0) {
        for (CSerializedMessage* const message : messages) {
            message->OnHandle(this);
            L2_RELEASE(message, ObjectRefType::SerializedMessage);
        }
        return;
    }

    for (CSerializedMessage* const message : messages) {
        message->Clear();
        L2_RELEASE(message, ObjectRefType::SerializedMessage);
    }
}

void CCreature::PushSerializedMessage(CSerializedMessage* message) {
    L2_GUARD_FUNCTION();
    if (!message) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CCreature::PushSerializedMessage() : "
        //     L"pSerializedMessage is NULL!")
        // Log.Add(
        //     LOG_ERROR,
        //     "[creature] Cannot queue a null serialized message for object %u.",
        //     m_nId)
        L2_LOG_ERROR(Log, "[creature] Cannot queue a null serialized message for object %u.", m_nId);
        runtime::process::ReportCallStack(2, true);
        return;
    }

    L2_GUARD(m_SerializerMessageLock);
    if (!m_bDeleting && m_Deleted == 0) {
        m_SerializedMessages.push_back(message);
    } else {
        message->Clear();
        L2_RELEASE(message, ObjectRefType::SerializedMessage);
    }
}

const char16_t* CCreature::GetName2(User*) { return m_data->Name(); }

bool CCreature::IsInvisibleBySkill() { return !m_data->m_StatusFlag[2] && m_SkillDurationMod.m_bHide; }

void CCreature::SetPeaceZone(bool peaceZone) { m_AreaMod.m_bIsInPeaceField = peaceZone; }

void CCreature::OnCreated() {}

void CCreature::OnEnterWorld(FVector&) {}

void CCreature::Say(const char16_t*) {}

CCreature* CCreature::GetDelegate() { return this; }

void CCreature::SetSpelledInfoChanged() {}

double CCreature::EquipedWeaponCriticalProb() const {
    const double armorProbability = EquipedArmorsValidate(VALIDATE::PCRITICAL);
    const double weaponProbability = EquipedWeaponValidate(VALIDATE::PCRITICAL);
    const double probability = armorProbability + weaponProbability;
    if (probability == 0.0) {
        return static_cast<double>(m_data->m_nOrgParam[VALIDATE::PCRITICAL]);
    }
    return probability;
}

::AttackType CCreature::GetAttackType() const { return m_data->m_AttackType; }

bool CCreature::CanDropItem(CItem* item, std::int64_t count, FVector& position, bool) {
    L2_GUARD_FUNCTION();

    CSharedItemData* const itemData = CItemFactory::Inst().GetData(item->m_data->index);
    if (!m_inv.Have(itemData->index) || itemData->m_nCount < count) {
        return false;
    }

    if (IsPC() && CastUser()->m_PledgeGameUser.IsWaiting()) {
        return false;
    }

    if (itemData->m_nCount < 1) {
        const char16_t* const name = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"try drop zero [%I64d][%I64d] [%d] User [%s]",
        //     itemData->m_nCount,
        //     count,
        //     itemData->m_nClassID,
        //     name)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Drop rejected: stored count %lld is below one (requested %lld, item class %d, owner %s).",
        //     static_cast<long long>(itemData->m_nCount),
        //     static_cast<long long>(count),
        //     itemData->m_nClassID,
        //     name)
        // L2_LOG_ERROR(Log,
        //     L"[inventory] Drop rejected: stored count %lld is below one (requested %lld, item class %d, owner %s).",
        //     static_cast<long long>(itemData->m_nCount),
        //     static_cast<long long>(count),
        //     itemData->m_nClassID,
        //     name)
        L2_LOG_ERROR(
            Log, u"[inventory] Drop rejected: stored count %lld is below one (requested %lld, item class %d, owner %s).",
            static_cast<long long>(itemData->m_nCount), static_cast<long long>(count), itemData->m_nClassID, name
        );
        if (IsPC()) {
            CastUser()->LogAbnormalAction(AUA_TRY_ITEM_NOT_ITEM, true, 0, 0, 0, 0);
        }
        return false;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    const int ground = geo->FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
    const std::uint32_t verticalDifference =
        static_cast<std::uint32_t>(ground - runtime::Win64CvttDoubleToInt32(position.z) + 100);
    if (verticalDifference > 200U) {
        const FVector source = GetPos();
        const double radius = m_data->m_dCollisionRadius;
        return CWorld::Inst().GetValidPositionAroundPos(*this, 0x28, radius, source, position);
    }

    FVector outputPosition{};
    bool distanceExhausted;
    double travelledDistance;

    User* lastAttacker = nullptr;
    CObject* const lastAttackerObject = CObject::GetObjectW(static_cast<std::uint32_t>(m_data->m_LastAttackerServerID));
    if (lastAttackerObject != nullptr && lastAttackerObject->IsPC()) {
        lastAttacker = lastAttackerObject->CastUser();
    }

    if (lastAttacker == nullptr || !IsBoss()) {
        const int inZoneId = static_cast<int>(GetInZoneID());
        const FVector source = GetPos();
        if (!CWorld::Inst().CanGo(source, position, outputPosition, distanceExhausted, travelledDistance, inZoneId, false)) {
            const FVector fallbackSource = GetPos();
            const double radius = m_data->m_dCollisionRadius;
            return CWorld::Inst().GetValidPositionAroundPos(*this, 0x28, radius, fallbackSource, position);
        }
        position = outputPosition;
        return true;
    }

    const int collisionZoneId = static_cast<int>(GetInZoneID());
    const FVector attackerPosition = lastAttacker->GetPos();
    if (g_WorldPlaneCollision.CheckCollision(attackerPosition, position, 0.0, outputPosition, collisionZoneId, false)) {
        position = outputPosition;
    }
    const int inZoneId = static_cast<int>(GetInZoneID());
    const FVector source = lastAttacker->GetPos();
    if (!CWorld::Inst().CanGo(source, position, outputPosition, distanceExhausted, travelledDistance, inZoneId, false)) {
        position = outputPosition;
    }
    return true;
}

double CCreature::EquipedArmorSpeedByCrystal() const {
    L2_GUARD_FUNCTION();
    double speed = 1.0;
    for (int slotIndex : m_data->m_nSlotIndex) {
        CSharedItemData* const item = CItemFactory::Inst().GetData(slotIndex);
        if (item && (item->m_Type == IT_ARMOR || item->m_Type == IT_ACCESSARY)) {
            CSharedItemConstant* const constant = item->GetConstant();
            const int crystal = static_cast<int>(constant->GetCrystalType());
            const int maximum = static_cast<int>(GetMaxCrystal());
            if (crystal < CRT_MAX && maximum < crystal) {
                speed *= 1.2;
            }
        }
    }
    return speed <= 2.0 ? speed : 2.0;
}

double CCreature::EquipedWeaponValidate(const VALIDATE::Enum type) const {
    // Current L2Server 0x005feea8.
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return 0.0;
    }

    CSharedItemConstant* const constant = weapon->GetConstant();
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(weapon->index);
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EquipedWeapon Validate failed. because "
        //     L"CItemFactory::Inst().GetObjectSP failed. "
        //     L"Name[%s]/Type[%d]/WeaponIndex[%d]/WeaponDBID[%d]",
        //     kCreatureServerSourceFile,
        //     0x1cb0,
        //     GetName(),
        //     static_cast<int>(type),
        //     weapon->m_nClassID,
        //     weapon->m_nDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[equipment] Cannot calculate weapon parameter %d for %s: item class %d, database ID %d has no live object at
        //     shared index %d, returning 0.", static_cast<int>(type), GetName(), weapon->m_nClassID, weapon->m_nDBID,
        //     weapon->index)
        // L2_LOG_ERROR(Log,
        //     L"[equipment] Cannot calculate weapon parameter %d for %s: item class %d, database ID %d has no live object at
        //     shared index %d, returning 0.", static_cast<int>(type), GetName(), weapon->m_nClassID, weapon->m_nDBID,
        //     weapon->index)
        L2_LOG_ERROR(
            Log,
            u"[equipment] Cannot calculate weapon parameter %d for %s: item class %d, database ID %d has no live object at "
            u"shared index %d; returning 0.",
            static_cast<int>(type), GetName(), weapon->m_nClassID, weapon->m_nDBID, weapon->index
        );
        return 0.0;
    }

    const int crystal = static_cast<int>(constant->GetCrystalType());
    double value = item->GetValidateParam(type);
    if (crystal >= CRT_NONE && crystal < CRT_MAX) {
        value += item->EnchantAddValue(type);
    }
    if ((type == VALIDATE::PATTACKSPEED || type == VALIDATE::MATTACKSPEED) && value == 0.0) {
        value = 1.0;
    }
    return value;
}

double CCreature::EquipedWeaponExtraValidate(VALIDATE::ExtraEnum type) const {
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return 0.0;
    }

    weapon->GetConstant();
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(weapon->index);
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] EquipedWeapon Validate failed. because "
        //     L"CItemFactory::Inst().GetObjectSP failed. "
        //     L"Name[%s]/Type[%d]/WeaponIndex[%d]/WeaponDBID[%d]",
        //     kCreatureServerSourceFile,
        //     0x1ce3,
        //     GetName(),
        //     static_cast<int>(type),
        //     weapon->m_nClassID,
        //     weapon->m_nDBID)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[equipment] Cannot calculate extra weapon parameter %d for %s: item class %d, database ID %d has no live
        //     object at shared index %d, returning 0.", static_cast<int>(type), GetName(), weapon->m_nClassID,
        //     weapon->m_nDBID,
        //     weapon->index)
        // L2_LOG_ERROR(Log,
        //     L"[equipment] Cannot calculate extra weapon parameter %d for %s: item class %d, database ID %d has no live
        //     object at shared index %d, returning 0.", static_cast<int>(type), GetName(), weapon->m_nClassID,
        //     weapon->m_nDBID,
        //     weapon->index)
        L2_LOG_ERROR(
            Log,
            u"[equipment] Cannot calculate extra weapon parameter %d for %s: item class %d, database ID %d has no live "
            u"object at shared index %d; returning 0.",
            static_cast<int>(type), GetName(), weapon->m_nClassID, weapon->m_nDBID, weapon->index
        );
        return 0.0;
    }
    return item->EnchantAddExtraValue(type);
}

int CCreature::GetArmorGradePenalty() const {
    L2_GUARD_FUNCTION();
    int penalty = 0;
    for (int slotIndex : m_data->m_nSlotIndex) {
        CSharedItemData* const item = CItemFactory::Inst().GetData(slotIndex);
        if (item && (item->m_Type == IT_ARMOR || item->m_Type == IT_ACCESSARY) &&
            item->GetConstant()->GetCrystalType() < CRT_MAX) {
            const int crystal = static_cast<int>(item->GetConstant()->GetCrystalType());
            const int maximum = static_cast<int>(GetMaxCrystal());
            if (maximum < crystal) {
                ++penalty;
            }
        }
    }
    return penalty <= 4 ? penalty : 4;
}

int CCreature::EquipedArmorsMpBonus() const {
    L2_GUARD_FUNCTION();
    std::int32_t bonus = 0;
    for (int slot = 0; slot < ::ST_MAX; ++slot) {
        CSharedItemData* const item = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[slot]);
        if (item && (item->m_Type == IT_ARMOR || item->m_Type == IT_ACCESSARY) && slot != ::ST_ONEPIECE) {
            bonus = runtime::AddInt32Wrapping(bonus, item->GetConstant()->m_sMpBonus);
        }
    }
    return bonus;
}

double CCreature::EquipedArmorsHpBonus() const {
    L2_GUARD_FUNCTION();
    double bonus = 0.0;
    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSharedItemData* const armor = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[slot]);
        if (armor == nullptr || armor->GetEnchanted() <= 0) {
            continue;
        }

        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(armor->index);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] EquipedArmorsHpBonus failed. "
            //     L"Name[%s]/Slot[%d]/ArmorIndex[%d]/ArmorDBID[%d]",
            //     kCreatureServerSourceFile,
            //     0x1ded,
            //     GetName(),
            //     slot,
            //     armor->m_nClassID,
            //     armor->m_nDBID)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[equipment] Cannot calculate the armor HP bonus for %s: slot %d, item class %d, database ID %d has no
            //     live object at shared index %d. Returning 0 for the entire bonus.", GetName(), slot, armor->m_nClassID,
            //     armor->m_nDBID,
            //     armor->index)
            // L2_LOG_ERROR(Log,
            //     L"[equipment] Cannot calculate the armor HP bonus for %s: slot %d, item class %d, database ID %d has no
            //     live object at shared index %d. Returning 0 for the entire bonus.", GetName(), slot, armor->m_nClassID,
            //     armor->m_nDBID,
            //     armor->index)
            L2_LOG_ERROR(
                Log,
                u"[equipment] Cannot calculate the armor HP bonus for %s: slot %d, item class %d, database ID %d has no "
                u"live object at shared index %d; returning 0 for the entire bonus.",
                GetName(), slot, armor->m_nClassID, armor->m_nDBID, armor->index
            );
            return 0.0;
        }
        bonus += item->GetEnchantedArmorHPBonus();
    }
    return bonus;
}

bool CCreature::ValidateArmorType() {
    L2_GUARD_FUNCTION();
    CSharedItemData* const chest = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_CHEST]);
    CSharedItemData* const legs = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_LEGS]);
    CSharedItemData* const onePiece = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_ONEPIECE]);
    CSharedItemData* const leftHand = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_LHAND]);

    ::ArmorType armorType = static_cast<::ArmorType>(0);
    if (chest && legs && chest->GetConstant()->m_ArmorType == legs->GetConstant()->m_ArmorType) {
        armorType = chest->GetConstant()->m_ArmorType;
    }
    if (onePiece) {
        armorType = onePiece->GetConstant()->m_ArmorType;
    }
    m_data->m_ArmorType = armorType;

    ::AdditionalArmorType additionalArmorType = static_cast<::AdditionalArmorType>(0);
    if (leftHand) {
        additionalArmorType = leftHand->GetConstant()->m_AdditionalArmorType;
    }
    m_data->m_AdditionalArmorType = additionalArmorType;
    return true;
}

CSharedItemData* CCreature::GetEquipedWeapon() const {
    L2_GUARD_FUNCTION();
    CSharedItemData* item = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_RLHAND]);
    if (item && item->m_Type == IT_WEAPON) {
        return item;
    }

    item = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_LHAND]);
    if (item && item->m_Type == IT_WEAPON) {
        return item;
    }

    item = CItemFactory::Inst().GetData(m_data->m_nSlotIndex[::ST_RHAND]);
    if (item && item->m_Type == IT_WEAPON) {
        return item;
    }
    return nullptr;
}

::AttackType CCreature::GetAttackType(CSharedItemData** itemData) {
    L2_GUARD_FUNCTION();
    CSharedItemData* const item = GetEquipedWeapon();
    *itemData = item;
    if (!item) {
        return m_data->m_BaseAttackType;
    }
    return item->GetConstant()->m_AttackType;
}

::AttackType CCreature::GetRealAttackType() {
    L2_GUARD_FUNCTION();
    CSharedItemData* const item = GetEquipedWeapon();
    if (!item) {
        return m_data->m_BaseAttackType;
    }
    return item->GetConstant()->m_AttackType;
}

int CCreature::EquipedWeaponSoulShotCount() {
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (!weapon) {
        return 0;
    }

    const int roll = runtime::Win64CvttDoubleToInt32(::genrand64_real1() * 100.0 + 0.0);
    if (roll < weapon->GetConstant()->m_Reduced_soulshot_prob) {
        return weapon->GetConstant()->m_Reduced_soulshot_count;
    }
    return weapon->GetConstant()->m_nSoulShotCount;
}

int CCreature::EquipedWeaponSpiritShotCount() {
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (!weapon) {
        return 0;
    }

    const int roll = runtime::Win64CvttDoubleToInt32(::genrand64_real1() * 100.0 + 0.0);
    if (roll < weapon->GetConstant()->m_Reduced_spiritshot_prob) {
        return weapon->GetConstant()->m_Reduced_spiritshot_count;
    }
    return weapon->GetConstant()->m_nSpiritShotCount;
}

int CCreature::GetWeaponGradePenalty() const {
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (!weapon || weapon->m_Type != IT_WEAPON) {
        return 0;
    }

    const int crystal = static_cast<int>(weapon->GetConstant()->GetCrystalType());
    const int maximum = static_cast<int>(GetMaxCrystal());
    if (crystal == CRT_EVENT || maximum >= crystal) {
        return 0;
    }

    const int adjustedCrystal = crystal > CRT_S80 ? crystal - 1 : crystal;
    const int adjustedMaximum = maximum > CRT_S80 ? maximum - 1 : maximum;
    const int penalty = adjustedCrystal - adjustedMaximum;
    return penalty <= 4 ? penalty : 4;
}

bool CCreature::IsInItemGradePenalty() const {
    L2_GUARD_FUNCTION();
    return GetWeaponGradePenalty() > 0 || EquipedArmorSpeedByCrystal() != 1.0;
}

SkillFxPositionType CCreature::GetDirectionAt(CCreature* creature) {
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    const FVector creaturePosition = creature->GetPos();
    FAngle creatureDirection = creature->GetDir();
    const double direction = std::atan2(creaturePosition.y - position.y, creaturePosition.x - position.x);
    FAngle directionAt{
        runtime::Win64CvttDoubleToInt32(direction * 65535.0 / 6.283185307179586),
    };

    const FAngle::Enum angle = creatureDirection.GetAngleDiff(directionAt);
    if (angle == FAngle::Side) {
        return SFPT_SIDE;
    }
    if (angle == FAngle::Back) {
        return SFPT_BACK;
    }
    return SFPT_FRONT;
}

double CCreature::CalcPositionDamage(CCreature& creature, CCreature& directionOwner, double damage) {
    L2_GUARD_FUNCTION();

    if (creature.IsPC()) {
        FAngle creatureDirection = directionOwner.GetDir();
        const FVector creaturePosition = creature.GetPos();
        const FVector position = GetPos();
        const double direction = std::atan2(position.y - creaturePosition.y, position.x - creaturePosition.x);
        FAngle directionAt{
            runtime::Win64CvttDoubleToInt32(direction * 65535.0 / 6.283185307179586),
        };

        FAngle::Enum angle = creatureDirection.GetAngleDiff(directionAt);
        if (creature.m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
            angle = ConvertSkillFxPositionTypeToAngleDir(creature.m_SkillMod.m_eChangeAttackPosition);
        }

        CCreatureSkillMod::ModName modName = CCreatureSkillMod::PositionDamageFront;
        if (angle == FAngle::Side) {
            modName = CCreatureSkillMod::PositionDamageSide;
        } else if (angle == FAngle::Back) {
            modName = CCreatureSkillMod::PositionDamageBack;
        }
        const CCreatureSkillMod::Mod& modifier = creature.m_SkillMod.m_Mods[modName];
        damage = damage * modifier.m_Per + modifier.m_Diff;
    }
    return damage;
}

bool CCreature::IsShieldDefend2(CCreature& attacker, const double probability) {
    L2_GUARD_FUNCTION();

    if (!m_SkillMod.m_bIsAllAngleShieldDefence) {
        const FVector position = GetPos();
        const FVector attackerPosition = attacker.GetPos();
        const double direction = std::atan2(position.y - attackerPosition.y, position.x - attackerPosition.x);
        FAngle directionAt{
            runtime::Win64CvttDoubleToInt32(direction * 65535.0 / 6.283185307179586),
        };
        FAngle currentDirection = GetDir();
        FAngle::Enum angle = directionAt.GetAngleDiff(currentDirection);
        if (attacker.m_SkillMod.m_eChangeAttackPosition != SFPT_MAX) {
            angle = ConvertSkillFxPositionTypeToAngleDir(attacker.m_SkillMod.m_eChangeAttackPosition);
        }
        if (angle != FAngle::Front) {
            return false;
        }
    }
    return ::genrand64_real1() * 100.0 + 0.0 < probability;
}

bool CCreature::IsShieldDefend(CCreature& attacker, const double probability) {
    L2_GUARD_FUNCTION();
    const double shieldDefenseRate = GetShieldDefenseRate();
    return IsShieldDefend2(attacker, shieldDefenseRate * probability);
}

double CCreature::CalcTimeFactorBonusForSkill(const double timeFactor, CSkillInfo& skillInfo) {
    L2_GUARD_FUNCTION();

    int magicType = skillInfo.m_nIsMagic;
    if (10 <= magicType && magicType < 14) {
        magicType = 0;
    } else if (20 <= magicType && magicType < 26) {
        magicType = 1;
    }

    const CCreatureSkillMod::Mod& modifier = m_SkillMod.m_MagicSpeedBonusByMagicType[magicType];
    const double adjusted = timeFactor * modifier.m_Per + modifier.m_Diff;
    const double capped = adjusted <= 6.006006006006006 ? adjusted : 6.006006006006006;
    return timeFactor <= capped ? capped : timeFactor;
}

double CCreature::CalcBasicCriticalRate(CObject* target) const {
    L2_GUARD_FUNCTION();

    if (target == nullptr || !target->IsCreature()) {
        return m_data->m_dParam[VALIDATE::PCRITICAL];
    }

    CCreature* const targetCreature = target->CastCreature();
    const CCreatureSkillMod::Mod& targetModifier = targetCreature->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceCriticalProb];
    const CCreatureSkillMod::Mod& modifier = m_SkillMod.m_Mods[CCreatureSkillMod::CriticalProb];
    const double dexterityBonus = DexBonusF();
    const double weaponCriticalProbability = EquipedWeaponCriticalProb();
    double criticalRate = targetModifier.m_Per * modifier.m_Per * dexterityBonus * weaponCriticalProbability +
        targetModifier.m_Diff + modifier.m_Diff;

    const int weaponGradePenalty = GetWeaponGradePenalty();
    if (0 < weaponGradePenalty) {
        criticalRate = criticalRate * (1.0 - static_cast<double>(weaponGradePenalty) * 0.1);
    }

    const double capped = criticalRate <= 50.0 ? criticalRate : 50.0;
    return 0.0 <= capped ? capped : 0.0;
}

bool CCreature::ValidateWeaponType() {
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (!weapon) {
        m_data->m_EquipedWeaponClassID = 0;
        if (m_data->m_AttackType != m_data->m_BaseAttackType) {
            m_data->m_AttackType = m_data->m_BaseAttackType;
            m_data->m_NeedUpdatePacketBitSet.SetBit(36);
        }
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nAttackRange, m_data->m_nBaseAttackRange, 37);
        m_data->m_nRandomDamage = m_data->m_nBaseRandomDamage;
        return true;
    }

    CSharedItemConstant* const constant = weapon->GetConstant();
    m_data->m_EquipedWeaponClassID = weapon->m_nClassID;
    const ::AttackType attackType = constant->m_AttackType;
    if (m_data->m_AttackType != attackType) {
        m_data->m_AttackType = attackType;
        m_data->m_NeedUpdatePacketBitSet.SetBit(36);
    }
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nAttackRange, constant->m_nAttackRange, 37);
    m_data->m_nRandomDamage = constant->m_nRandomDamage;
    m_ItemMod.m_bNoAttackWeapon = constant->m_bNoAttackWeapon;
    m_ItemMod.m_bOnlyUseEquipedWeaponActiveSkill = constant->m_bOnlyUseEquipedWeaponActiveSkill;
    m_ItemMod.m_bHpLimitBreak = constant->m_bHpLimitBreak;
    return true;
}

void CCreature::ValidateGradePenalty(VALIDATE::Enum type) {
    L2_GUARD_FUNCTION();
    bool applyWeapon = false;
    bool applyArmor = false;
    switch (type) {
        case VALIDATE::PDEFEND:
        case VALIDATE::MDEFEND:
        case VALIDATE::SPEED:
        case VALIDATE::PAVOID:
        case VALIDATE::MAVOID:
            applyArmor = true;
            break;
        case VALIDATE::PATTACK:
        case VALIDATE::MATTACK:
        case VALIDATE::PHIT:
        case VALIDATE::MHIT:
        case VALIDATE::PCRITICAL:
        case VALIDATE::MCRITICAL:
            applyWeapon = true;
            break;
        case VALIDATE::PATTACKSPEED:
        case VALIDATE::MATTACKSPEED:
        case VALIDATE::PSKILLSPEED:
            applyWeapon = true;
            applyArmor = true;
            break;
        default:
            return;
    }

    double& parameter = m_data->m_dParam[static_cast<int>(type)];
    if (applyWeapon) {
        const double penalty = static_cast<double>(GetWeaponGradePenalty());
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(parameter, ((10.0 - penalty) / 10.0) * parameter, 59);
    }
    if (applyArmor) {
        const double penalty = static_cast<double>(GetArmorGradePenalty());
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(parameter, ((10.0 - penalty) / 10.0) * parameter, 59);
    }
}

std::int64_t CCreature::ExpInc(std::int64_t experience, bool) {
    return SetExp(runtime::AddInt64Wrapping(m_data->m_nExp, experience));
}

bool CCreature::BeResurrectAsked(CCreature* resurrector, int resurrectExpRatio, bool immediately) {
    // Current L2Server.exe 0x00620374.
    L2_GUARD_FUNCTION();
    if (IsAlive()) {
        return false;
    }

    const std::int64_t resurrectExp = GetResurrectExp(resurrector, static_cast<double>(resurrectExpRatio));
    bool petResurrection = false;
    User* targetOwner = nullptr;
    CPet* targetPet = nullptr;

    if (IsPC()) {
        targetOwner = CastUser();
        targetPet = targetOwner->GetPet();
    } else if (IsPet()) {
        targetPet = CastPet();
        targetOwner = targetPet->GetMaster();
        if (targetOwner == nullptr) {
            return false;
        }
        petResurrection = true;
    } else {
        ResurrectBy(resurrector, resurrectExp);
        return true;
    }

    if (targetOwner != nullptr) {
        User* const resurrectorAccountIdUser = resurrector->CastUser();
        User* const resurrectorDatabaseIdUser = resurrector->CastUser();
        User* const resurrectorAccountNameUser = resurrector->CastUser();
        const double positionZ = resurrector->GetPos().z;
        const double positionY = resurrector->GetPos().y;
        const double positionX = resurrector->GetPos().x;
        const char16_t* const targetName = targetOwner->GetName();
        const char16_t* const resurrectorName = resurrector->GetName();
        const std::uint32_t targetAccountId = targetOwner->GetAccountID();
        const std::uint32_t targetDatabaseId = targetOwner->GetDBID();
        const std::uint32_t resurrectorAccountId = resurrectorAccountIdUser->GetAccountID();
        const std::uint32_t resurrectorDatabaseId = resurrectorDatabaseIdUser->GetDBID();
        // Account names are target UTF-16. This formatter consumes native wide strings.
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
        //     static_cast<std::int64_t>(0x462),
        //     static_cast<std::int64_t>(resurrectorDatabaseId),
        //     static_cast<std::int64_t>(resurrectorAccountId),
        //     static_cast<std::int64_t>(targetDatabaseId),
        //     static_cast<std::int64_t>(targetAccountId),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionX)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionY)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionZ)),
        //     static_cast<std::int64_t>(resurrector->m_data->m_Race),
        //     static_cast<std::int64_t>(resurrector->m_data->m_Sex),
        //     static_cast<std::int64_t>(resurrector->m_data->m_nClass),
        //     static_cast<std::int64_t>(resurrector->m_data->m_nLevel),
        //     resurrectExp,
        //     static_cast<std::int64_t>(petResurrection ? 1 : 0),
        //     resurrectorName,
        //     resurrectorAccountNameUser->m_sAccountName.data(),
        //     targetName,
        //     targetOwner->m_sAccountName.data())
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
        //     static_cast<std::int64_t>(0x462),
        //     static_cast<std::int64_t>(resurrectorDatabaseId),
        //     static_cast<std::int64_t>(resurrectorAccountId),
        //     static_cast<std::int64_t>(targetDatabaseId),
        //     static_cast<std::int64_t>(targetAccountId),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionX)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionY)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(positionZ)),
        //     static_cast<std::int64_t>(resurrector->m_data->m_Race),
        //     static_cast<std::int64_t>(resurrector->m_data->m_Sex),
        //     static_cast<std::int64_t>(resurrector->m_data->m_nClass),
        //     static_cast<std::int64_t>(resurrector->m_data->m_nLevel),
        //     resurrectExp,
        //     static_cast<std::int64_t>(petResurrection ? 1 : 0),
        //     resurrectorName,
        //     Utf16ToWide(resurrectorAccountNameUser->m_sAccountName.data()).c_str(),
        //     targetName,
        //     Utf16ToWide(targetOwner->m_sAccountName.data()).c_str())
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
            static_cast<std::int64_t>(0x462), static_cast<std::int64_t>(resurrectorDatabaseId),
            static_cast<std::int64_t>(resurrectorAccountId), static_cast<std::int64_t>(targetDatabaseId),
            static_cast<std::int64_t>(targetAccountId),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)),
            static_cast<std::int64_t>(resurrector->m_data->m_Race), static_cast<std::int64_t>(resurrector->m_data->m_Sex),
            static_cast<std::int64_t>(resurrector->m_data->m_nClass),
            static_cast<std::int64_t>(resurrector->m_data->m_nLevel), resurrectExp,
            static_cast<std::int64_t>(petResurrection ? 1 : 0), resurrectorName,
            resurrectorAccountNameUser->m_sAccountName.data(), targetName, targetOwner->m_sAccountName.data()
        );
    }

    if (petResurrection) {
        if (targetOwner->m_data->m_bCanResurrection != 0) {
            targetOwner->SendSystemMessage(SystemMessageId::PET_RESURRECTION_INVALIDATED_OWNER_RESURRECTING);
            return false;
        }
    } else if (targetPet != nullptr && targetPet->m_data->m_bCanResurrection != 0) {
        targetOwner->SendSystemMessage(SystemMessageId::OWNER_RESURRECTION_INVALIDATED_PET_RESURRECTING);
        return false;
    }

    if (m_data->m_bCanResurrection != 0 && m_data->m_nResurrectExp > resurrectExp) {
        resurrector->SendSystemMessage(SystemMessageId::BETTER_RESURRECTION_ALREADY_OFFERED);
        return false;
    }

    if (targetOwner != nullptr && (!IsPC() || targetOwner->m_data->m_bIsAlive == 0)) {
        runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 1);
        std::memset(m_data->m_sResurrectSpellerName, 0, sizeof(m_data->m_sResurrectSpellerName));
        const std::u16string_view resurrectorName(resurrector->GetName());
        std::copy_n(
            resurrectorName.data(), std::min<std::size_t>(resurrectorName.size(), 24), m_data->m_sResurrectSpellerName
        );
        m_data->m_nResurrectExp = resurrectExp;
        m_data->m_dwResurrectTime = runtime::GetTickCount();
        if (!immediately) {
            SendResurrectionConfirmDlg();
        } else {
            ResurrectBy(nullptr, m_data->m_nResurrectExp);
        }
    }
    return true;
}

bool CCreature::ResurrectBy(CCreature* resurrector, std::int64_t resurrectExp) {
    // Current L2Server.exe 0x0061cfe8.
    L2_GUARD_FUNCTION();

    bool useSpecialRecovery;
    std::optional<FVector> positionStorage;
    {
        L2_GUARD(*m_lock);

        if (m_bLeft || m_data->m_bIsAlive != 0 || !m_data->m_bIsActive || !m_data->m_bCanBeResurrected) {
            return false;
        }

        m_nDeathCount.store(0, std::memory_order_relaxed);

        const auto& allowedInstantZones = m_SkillDurationMod.m_vResurrectionInstantZoneIds;
        const bool instantZoneAllowed = allowedInstantZones.empty() ||
            std::find(
                allowedInstantZones.begin(), allowedInstantZones.end(), static_cast<int>(m_data->m_nInstantZoneTypeID)
            ) != allowedInstantZones.end();
        useSpecialRecovery = m_SkillDurationMod.m_nResurrectionSpecial > 0 && instantZoneAllowed;

        if (!useSpecialRecovery) {
            m_data->SetHP(m_data->m_dBaseHP * 0.7);
        } else {
            double hpRatio = m_SkillDurationMod.m_dRecoverHPPer / 100.0;
            if (hpRatio < 0.7) {
                hpRatio = 0.7;
            }
            m_data->SetHP(m_data->m_dBaseHP * hpRatio);

            const double recoveredMP = (m_SkillDurationMod.m_dRecoverMPPer / 100.0) * m_data->m_dBaseMP;
            m_data->SetMP(std::max(m_data->m_dMP, recoveredMP));

            const double recoveredCP = (m_SkillDurationMod.m_dRecoverCPPer / 100.0) * m_data->m_dBaseCP;
            m_data->SetCP(std::max(m_data->m_dCP, recoveredCP));

            --m_SkillDurationMod.m_nResurrectionSpecial;
            if (m_SkillDurationMod.m_nResurrectionSpecial < 0) {
                m_SkillDurationMod.m_nResurrectionSpecial = 0;
            }
            CheckAbnormalVisualEffect();
        }

        const FVector& position = positionStorage.emplace(GetPos());
        User* logUser = nullptr;
        int petResurrection = 0;
        std::wstring actorNameStorage;
        const wchar_t* actorName = nullptr;
        std::uint32_t actorAccountId = 0;
        std::uint32_t actorDatabaseId = 0;

        if (IsPC()) {
            User* const user = CastUser();
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_bIsAlive, 1, 0x61);
            user->ExpInc(resurrectExp, true);
            user->m_bIsNewInWorld = true;
            db.SaveDieDropExp(*user, 0);
            m_nDownExp = 0;
            db.SendSaveCharacterInfo(user, true, true);
            logUser = user;
            actorNameStorage = Utf16ToWide(user->GetName());
            actorName = actorNameStorage.c_str();
            actorAccountId = user->GetAccountID();
            actorDatabaseId = user->GetDBID();
        } else if (IsNPC()) {
            CNPC* const npc = CastNPC();
            if (npc->IsPet()) {
                m_nDead = 0;
                ExpInc(resurrectExp, true);
                m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_bIsAlive, 1, 0x61);
                CPet* const pet = CastPet();
                pet->m_nDownExp = 0;
                runtime::raw_atomic::AddI32(&pet->m_nSaveToDBTick, 0x2d);
                if (pet->IsSyncLevel()) {
                    pet->ValidateSyncLevel();
                }
                User* const master = pet->GetMaster();
                if (master != nullptr) {
                    logUser = master;
                    petResurrection = 1;
                    actorNameStorage = Utf16ToWide(master->GetName());
                    actorName = actorNameStorage.c_str();
                    actorAccountId = master->GetAccountID();
                    actorDatabaseId = master->GetDBID();
                }
            }
        }

        if (logUser != nullptr) {
            const std::wstring actorAccountName = Utf16ToWide(logUser->m_sAccountName.data());
            const std::wstring targetName = Utf16ToWide(m_data->Name());

            // Match the native variadic argument widths. Preserve the accounting CSV schema.
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,%s,,",
            //     0x45e,
            //     actorDatabaseId,
            //     actorAccountId,
            //     0,
            //     0,
            //     runtime::Win64CvttDoubleToInt32(position.x),
            //     runtime::Win64CvttDoubleToInt32(position.y),
            //     runtime::Win64CvttDoubleToInt32(position.z),
            //     logUser->m_data->m_Race,
            //     logUser->m_data->m_Sex,
            //     logUser->m_data->m_nClass,
            //     logUser->m_data->m_nLevel,
            //     resurrectExp,
            //     m_data->m_nExp,
            //     petResurrection,
            //     actorName,
            //     actorAccountName.c_str(),
            //     targetName.c_str())
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,%s,,",
                static_cast<long long>(0x45e), static_cast<long long>(actorDatabaseId),
                static_cast<long long>(actorAccountId), static_cast<long long>(0), static_cast<long long>(0),
                static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
                static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
                static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
                static_cast<long long>(logUser->m_data->m_Race), static_cast<long long>(logUser->m_data->m_Sex),
                static_cast<long long>(logUser->m_data->m_nClass), static_cast<long long>(logUser->m_data->m_nLevel),
                resurrectExp, m_data->m_nExp, static_cast<long long>(petResurrection), actorName, actorAccountName.c_str(),
                targetName.c_str()
            );
        }
    }

    const FVector& position = *positionStorage;
    if (IsPC()) {
        User* const user = CastUser();
        user->ValidateChanged(VB_MAX);
        ParamChanged(std::int64_t{PCB_USER});
    } else {
        ValidateChanged(VB_MAX);
    }

    if (useSpecialRecovery) {
        ActivateRecoverSkill();
    }

    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborContainer(this, 0x80000, position, "cd", 1, static_cast<int>(Id()));
    } else if (IsPC()) {
        User* const user = CastUser();
        if (CUserSocket* const socket = user->GetSocket()) {
            socket->Send("cd", 1, static_cast<int>(Id()));
        }
    }

    if (IsNPC() && !IsPet() && resurrector != nullptr) {
        g_npcserver.Send("cddd", 0x0d, static_cast<int>(resurrector->Id()), resurrector->GetIndex(), GetIndex());
    }

    runtime::raw_atomic::ExchangeI32(&m_data->m_bCanResurrection, 0);
    m_data->m_nResurrectExp = 0;
    m_data->m_nResurrectRatio = 0;
    m_data->m_dwResurrectTime = runtime::GetTickCount();

    if (IsPC()) {
        User* const user = CastUser();
        user->m_bParamHPCPMPChanged = true;
        user->m_nParamHPCPMPChanged |= 1;
        user->BroadcastHP(1, 0);
        g_npcserver.Send("cddd", 0x30, static_cast<int>(Id()), 0, 3);
    }

    return true;
}

void CCreature::IncrementParam(ParameterType parameter, std::int64_t amount) {
    // Current L2Server.exe 0x005e6b3c.
    L2_GUARD_FUNCTION();

    if (parameter == PARAM_EXP) {
        if (!IsPC()) {
            return;
        }
        CUserSocket* socket = nullptr;
        int messageId = 0;
        std::uint64_t displayed = 0;
        if (amount < 0) {
            ExpInc(amount, false);
            socket = CastUser()->GetSocket();
            messageId = SystemMessageId::EXPERIENCE_DECREASED;
            displayed = std::uint64_t{0} - static_cast<std::uint64_t>(amount);
        } else {
            ExpInc(amount, false);
            socket = CastUser()->GetSocket();
            messageId = SystemMessageId::GAINED_EXPERIENCE;
            displayed = static_cast<std::uint64_t>(amount);
        }
        socket->Send("chccQ", 0x62, messageId, 1, 6, displayed);
        return;
    }

    if (parameter == PARAM_SP) {
        SPInc(amount);
        if (!IsPC()) {
            return;
        }
        CUserSocket* socket = nullptr;
        int messageId = 0;
        std::uint64_t displayed = 0;
        if (amount < 0) {
            socket = CastUser()->GetSocket();
            messageId = SystemMessageId::SP_DECREASED;
            displayed = std::uint64_t{0} - static_cast<std::uint64_t>(amount);
        } else {
            socket = CastUser()->GetSocket();
            messageId = SystemMessageId::OBTAINED_SP;
            displayed = static_cast<std::uint64_t>(amount);
        }
        socket->Send("chccQ", 0x62, messageId, 1, 6, displayed);
        return;
    }

    if (parameter >= PARAM_INT && parameter <= PARAM_CHA) {
        const int statIndex = static_cast<int>(parameter) - static_cast<int>(PARAM_INT);
        std::int32_t& value = m_data->m_Stat.m_N[statIndex];
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            value, runtime::AddInt32Wrapping(value, static_cast<std::int32_t>(amount)), 0x3c
        );
        return;
    }

    if (parameter == PARAM_LEVEL) {
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            m_data->m_nLevel, runtime::AddInt32Wrapping(m_data->m_nLevel, static_cast<std::int32_t>(amount)), 5
        );
        return;
    }

    if (parameter != PARAM_PKCOUNT) {
        return;
    }

    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        m_data->m_nPKCount, runtime::AddInt32Wrapping(m_data->m_nPKCount, static_cast<std::int32_t>(amount)), 0x1e
    );
    if (m_data->m_nPKCount < 0) {
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nPKCount, 0, 0x1e);
    }
    if (m_data->m_nPKPardonCount > m_data->m_nPKCount) {
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nPKPardonCount, m_data->m_nPKCount, 0x1d);
    }
}

void CCreature::SetGuilty(char guilty) {
    m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(m_data->m_cIsGuilty, guilty, 38);
}

bool CCreature::IsGuilty() { return GetGuilty() > 0; }

void CCreature::SetKarma(int karma) {
    if (karma < -100000000) {
        karma = -100000000;
    }
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nKarma, karma, 69);
}

void CCreature::ChangeKarmaByDelta(int delta, bool broadcast) {
    if (g_bInterludeKarma) {
        // No reputation above zero: a stored positive value counts as 0.
        SetKarma(std::min(runtime::AddInt32Wrapping(std::min(m_data->m_nKarma, 0), delta), 0));
        return;
    }
    if (!broadcast) {
        const int currentKarma = m_data->m_nKarma;
        if (currentKarma <= 0 && runtime::AddInt32Wrapping(currentKarma, delta) > 0) {
            SetKarma(0);
            return;
        }
    }

    SetKarma(runtime::AddInt32Wrapping(m_data->m_nKarma, delta));
    if (m_data->m_nKarma > 500) {
        SetKarma(500);
    }
}

CrystalType CCreature::GetMaxCrystal() const { return CRT_NONE; }

double CCreature::RegenBonusByMotionEx() { return 1.0; }

double CCreature::HPRegenBonusByMotionEx() { return 1.0; }

double CCreature::MPRegenBonusByMotionEx() { return 1.0; }

double CCreature::CPRegenBonusByMotionEx() { return 1.0; }

double CCreature::LevelBonus() const { return g_PCParamDB.GetParamBonus(PC_LEVEL_BONUS, m_data->m_nLevel); }

double CCreature::GetAttackModifier() {
    if (m_data->m_nOrgParam[4] <= 0) {
        return 1.0;
    }
    return 1.8 / m_data->m_dAttackTime;
}

double CCreature::GetMoveModifier() {
    if (m_data->m_nMoveTypeSpeed <= 0) {
        m_data->m_nMoveTypeSpeed = 1;
    }
    return m_data->m_dParam[11] / static_cast<double>(m_data->m_nMoveTypeSpeed);
}

double CCreature::GetPAttack() {
    const double value = m_data->m_dParam[2];
    return value < 0.0 ? 0.0 : value;
}

double CCreature::GetPHit() {
    const double value = m_data->m_dParam[7];
    return value < 0.0 ? 0.0 : value;
}

double CCreature::GetMHit() {
    const double value = m_data->m_dParam[8];
    return value < 0.0 ? 0.0 : value;
}

double CCreature::GetPAvoid() {
    const double value = m_data->m_dParam[14];
    return value < 0.0 ? 0.0 : value;
}

double CCreature::GetPDefend() {
    const double value = m_data->m_dParam[0];
    return value < 0.0 ? 0.0 : value;
}

double CCreature::GetShieldDefenseRate() { return m_data->m_dParam[13]; }

double CCreature::GetShieldDefense() { return m_data->m_dParam[12]; }

void CCreature::ApplyGotDamageBySkill(CCreature* attacker, double damage, double appliedDamage) {
    // Current L2Server 0x005e3834.
    L2_GUARD_FUNCTION();
    if (attacker == nullptr) {
        return;
    }

    if (m_SkillMod.CanGetDamageHP(attacker, this)) {
        if (m_SkillMod.m_bIsReverseAttack) {
            double reverseDamage = damage * m_SkillMod.m_dReverseDamagePer - damage;
            const double resist = attacker->m_SkillDurationMod.m_dDamageShieldResist;
            if (resist >= 0.0) {
                const double firstClamp = std::min(resist, 100.0);
                if (firstClamp > 0.0) {
                    const double clampedResist = std::clamp(resist, 0.0, 100.0);
                    reverseDamage *= (100.0 - clampedResist) / 100.0;
                }
            }

            reverseDamage = std::min(reverseDamage, GetPDefend() + GetShieldDefense());
            if (static_cast<int>(reverseDamage) == 0) {
                return;
            }
            attacker->GotDamageBy(this, reverseDamage, false, DAMAGE_SHIELD, true);
        }

        if (attacker->m_SkillMod.m_bIsVampiricState &&
            genrand64_real1() * 100.0 <= attacker->m_SkillMod.m_dVampiricProb * 100.0) {
            attacker->IncreaseHP_diff(
                (attacker->m_SkillMod.m_dVampiricBonusPer - 1.0) * appliedDamage * attacker->m_dMeleeMultiTargetBias *
                    (1.0 - m_SkillMod.m_dVampiricDefencePer),
                false, 4, 0, false
            );
        }

        double mpBonus = 0.0;
        for (int index = 0; index < attacker->m_SkillMod.m_nMPVampiricIndex; ++index) {
            const CCreatureSkillMod::MPVampiric& vampiric = attacker->m_SkillMod.m_MPVampiric[index];
            if (vampiric.m_dProb > 0.0 && genrand64_real1() * 100.0 < vampiric.m_dProb) {
                mpBonus += vampiric.m_dBonus;
            }
        }
        if (mpBonus > 0.0) {
            attacker->IncreaseMP_diff(mpBonus / 100.0 * appliedDamage * attacker->m_dMeleeMultiTargetBias, 4, 0, false);
        }
    }

    if (attacker->GetAttackType() == AT_POLE) {
        attacker->m_dMeleeMultiTargetBias *= 0.5;
    }
}

bool CCreature::CanUseShot(const CSkillInfo*) {
    L2_GUARD_FUNCTION();
    return true;
}

void CCreature::DisableSetItemSkill() {
    // Current L2Server 0x0061af34.
    L2_GUARD_FUNCTION();
    if (m_nSetItemClassId <= 0) {
        return;
    }
    CSetItem* const setItem = g_ObjectDB.GetSetItemBySetItemId(m_nSetItemClassId);
    if (setItem != nullptr) {
        for (CSkillInfo* const skill : setItem->m_pSkills) {
            if (skill != nullptr) {
                DeleteAcquireSkill(skill->m_nSkillId);
            }
        }
    }
    m_nSetItemClassId = 0;
}

void CCreature::EnableSetItemSkill(int setItemId, bool additional2) {
    // Current L2Server 0x00619b64.
    L2_GUARD_FUNCTION();
    if (setItemId <= 0) {
        return;
    }
    CSetItem* const setItem = g_ObjectDB.GetSetItemBySetItemId(setItemId);
    if (setItem != nullptr) {
        for (int index = CSetItem::SIS_Skill; index <= CSetItem::SIS_SetSkill; ++index) {
            CSkillInfo* const skill = setItem->m_pSkills[index];
            if (skill != nullptr) {
                AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, true);
            }
        }

        const int additionalSlot = static_cast<int>(setItem->m_nAdditionalItemSlot);
        if (additionalSlot != 0 && m_data->m_nSlotIndex[additionalSlot] != -1) {
            CItem* const item = m_inv.GetByIndex(m_data->m_nSlotIndex[additionalSlot]);
            if (item != nullptr &&
                setItem->SlotToSetItemClassID(additionalSlot, item->m_data->m_nClassID) == item->m_data->m_nClassID) {
                CSkillInfo* const skill = setItem->m_pSkills[CSetItem::SIS_AdditionalSetSkill];
                if (skill != nullptr) {
                    AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, true);
                }
            }
        }
        if (additional2) {
            CSkillInfo* const skill = setItem->m_pSkills[CSetItem::SIS_Additional2SetSkill];
            if (skill != nullptr) {
                AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, true);
            }
        }
    }
    m_nSetItemClassId = setItemId;
}

void CCreature::SetItemSkillEnDisable(int setItemId, bool additional2) {
    // Current L2Server 0x0061b018.
    L2_GUARD_FUNCTION();
    if (setItemId < 1) {
        if (m_nSetItemClassId > 0) {
            DisableSetItemSkill();
        }
        return;
    }
    if (m_nSetItemClassId < 1) {
        EnableSetItemSkill(setItemId, additional2);
        return;
    }

    CSetItem* const next = g_ObjectDB.GetSetItemBySetItemId(setItemId);
    CSetItem* const previous = g_ObjectDB.GetSetItemBySetItemId(m_nSetItemClassId);
    if (next != nullptr && previous != nullptr) {
        CSkillInfo* nextSkill = next->m_pSkills[CSetItem::SIS_Skill];
        CSkillInfo* previousSkill = previous->m_pSkills[CSetItem::SIS_Skill];
        if (nextSkill != nullptr && previousSkill != nullptr &&
            (nextSkill->m_nSkillId != previousSkill->m_nSkillId || nextSkill->m_nLevel != previousSkill->m_nLevel)) {
            DeleteAcquireSkill(previousSkill->m_nSkillId);
            AcquireSkill(nextSkill->m_nSkillId, nextSkill->m_nLevel, nextSkill->m_nSubLevel, false, true);
        }

        nextSkill = next->m_pSkills[CSetItem::SIS_SetSkill];
        previousSkill = previous->m_pSkills[CSetItem::SIS_SetSkill];
        if (nextSkill != nullptr && previousSkill != nullptr &&
            (nextSkill->m_nSkillId != previousSkill->m_nSkillId || nextSkill->m_nLevel != previousSkill->m_nLevel)) {
            DeleteAcquireSkill(previousSkill->m_nSkillId);
            AcquireSkill(nextSkill->m_nSkillId, nextSkill->m_nLevel, nextSkill->m_nSubLevel, false, true);
        }

        for (int index = CSetItem::SIS_AdditionalSetSkill; index < CSetItem::SIS_Max; ++index) {
            CSkillInfo* const skill = previous->m_pSkills[index];
            if (skill != nullptr) {
                DeleteAcquireSkill(skill->m_nSkillId);
            }
        }

        const int additionalSlot = static_cast<int>(next->m_nAdditionalItemSlot);
        if (additionalSlot != 0 && m_data->m_nSlotIndex[additionalSlot] != -1) {
            CItem* const item = m_inv.GetByIndex(m_data->m_nSlotIndex[additionalSlot]);
            if (item != nullptr &&
                next->SlotToSetItemClassID(additionalSlot, item->m_data->m_nClassID) == item->m_data->m_nClassID) {
                CSkillInfo* const skill = next->m_pSkills[CSetItem::SIS_AdditionalSetSkill];
                if (skill != nullptr) {
                    AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, true);
                }
            }
        }
        if (additional2) {
            CSkillInfo* const skill = next->m_pSkills[CSetItem::SIS_Additional2SetSkill];
            if (skill != nullptr) {
                AcquireSkill(skill->m_nSkillId, skill->m_nLevel, skill->m_nSubLevel, false, true);
            }
        }
    }
    m_nSetItemClassId = setItemId;
}

bool CCreature::ValidateSetItem(SlotType) {
    // Current L2Server 0x0061b4e4. The slot parameter is not read.
    L2_GUARD_FUNCTION();
    bool additional2 = false;
    const int setItemId = g_ObjectDB.IsItSet(this, m_data->m_nSlotIndex, additional2);
    static_cast<void>(IsPC());
    SetItemSkillEnDisable(setItemId, additional2);
    return setItemId != 0;
}

void CCreature::_ValidateSetItemOnEquipItem(CItem*, SlotType slotType, bool) {
    // Current L2Server 0x0061ca60.
    L2_GUARD_FUNCTION();
    ValidateSetItem(slotType);
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_MAX);
    } else {
        ValidateChanged(VB_MAX);
    }
}

void CCreature::_ValidateSetItemOnUnequipItem(SlotType slotType, bool) {
    // Current L2Server 0x0061cb44.
    L2_GUARD_FUNCTION();
    ValidateSetItem(slotType);
    if (IsPC()) {
        CastUser()->ValidateChanged(VB_MAX);
    } else {
        ValidateChanged(VB_MAX);
    }
}

void CCreature::GetChestSlotNewSetItemInfo(std::map<int, int>& setItems, bool& fullSet) {
    // Current L2Server 0x006077cc.
    L2_GUARD_FUNCTION();
    CSPointer<CItem> chest = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_CHEST]);
    if (!chest) {
        return;
    }
    CItemConstInfo* const chestInfo = g_ItemConstInfoDB.GetItemConstInfo(chest->GetClassId());
    if (chestInfo == nullptr) {
        return;
    }

    std::vector<int> chestSetIds;
    chestInfo->GetNewSetItemIDList(chestSetIds);
    if (chestSetIds.empty()) {
        const bool changed = m_data->m_NeedUpdatePacketBitSet.SetShortValue_IfModifed(
            reinterpret_cast<std::int16_t&>(m_hMinNewSetItemEnchantedEffect), 0, 0x4e
        );
        m_hMinNewSetItemEnchanted = 0;
        if (changed && IsPC()) {
            CastUser()->ParamChanged(PCB_USER);
        }
        return;
    }

    const int setItemId = chestSetIds.front();
    if (setItemId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d] [ChestSlotNewSet] invalid "
        //     L"pChestItemConstInfo->GetNewSetItemID()=%d",
        //     kCreatureServerSourceFile,
        //     0x2b09,
        //     0)
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Chest-set evaluation stopped for object %u: the first set ID is %d.",
        //     m_nId,
        //     0)
        L2_LOG_ERROR(Log, "[equipment] Chest-set evaluation stopped for object %u: the first set ID is %d.", m_nId, 0);
        return;
    }
    if (setItems.find(setItemId) != setItems.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d] [ChestSlotNewSet] already exist "
        //     L"mapNewSetItem(%d,%d)",
        //     kCreatureServerSourceFile,
        //     0x2b11,
        //     setItemId,
        //     setItems[setItemId])
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Chest-set evaluation stopped for object %u: set %d already has a part-count entry of %d.",
        //     m_nId,
        //     setItemId,
        //     setItems[setItemId])
        L2_LOG_ERROR(
            Log, "[equipment] Chest-set evaluation stopped for object %u: set %d already has a part-count entry of %d.",
            m_nId, setItemId, setItems[setItemId]
        );
        return;
    }
    setItems.emplace(setItemId, 1);

    CNewSetItem* const setItem = g_ObjectDB.GetNewSetItemBySetItemId(setItemId);
    if (setItem == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ERROR][%s][%d] [ChestSlotNewSet] invalid "
        //     L"ChestSlotNewSetItemID=%d",
        //     kCreatureServerSourceFile,
        //     0x2b1b,
        //     setItemId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Chest-set evaluation stopped for object %u: set %d has no definition. Its part-count entry was
        //     already added.", m_nId, setItemId)
        L2_LOG_ERROR(
            Log,
            "[equipment] Chest-set evaluation stopped for object %u: set %d has no definition; its part-count entry was "
            "already added.",
            m_nId, setItemId
        );
        return;
    }

    const int requiredEnchant = setItem->m_nEnchantNum;
    bool enchantValid = requiredEnchant <= chest->m_data->GetEnchanted();
    int minimumEnchant = 0;
    int minimumEffectEnchant = 0;
    if (enchantValid) {
        const int crystal = static_cast<int>(chest->m_const->GetCrystalType());
        if (crystal > 5 && crystal < 11) {
            minimumEnchant = chest->m_data->GetEnchanted();
        }
        if (crystal > 7 && crystal < 11) {
            minimumEffectEnchant = chest->m_data->GetEnchanted();
        }
    }

    for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
        if (slot == ST_LHAND || slot == ST_CHEST || slot == ST_ONEPIECE) {
            continue;
        }
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (!item) {
            continue;
        }
        CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(item->GetClassId());
        if (itemInfo == nullptr) {
            continue;
        }
        std::vector<int> itemSetIds;
        itemInfo->GetNewSetItemIDList(itemSetIds);
        for (const int itemSetId : itemSetIds) {
            if (itemSetId != setItemId) {
                continue;
            }
            const auto position = setItems.find(itemSetId);
            if (position == setItems.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[ERROR][%s][%d] [ChestSlotNewSet] invalid "
                //     L"EquipSlotNewSetID=%d",
                //     kCreatureServerSourceFile,
                //     0x2b72,
                //     itemSetId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[equipment] Skipping slot %d while evaluating the chest set for object %u: set %d has no part-count
                //     entry.", slot, m_nId, itemSetId)
                L2_LOG_ERROR(
                    Log,
                    "[equipment] Skipping slot %d while evaluating the chest set for object %u: set %d has no part-count "
                    "entry.",
                    slot, m_nId, itemSetId
                );
                continue;
            }
            ++position->second;
            if (!enchantValid || requiredEnchant == 0) {
                continue;
            }
            const int enchanted = item->m_data->GetEnchanted();
            if (enchanted < requiredEnchant) {
                enchantValid = false;
                minimumEnchant = 0;
                minimumEffectEnchant = 0;
                continue;
            }
            const int crystal = static_cast<int>(item->m_const->GetCrystalType());
            if (crystal > 5 && crystal < 11) {
                minimumEnchant = std::min(minimumEnchant, enchanted);
            }
            if (crystal > 7 && crystal < 11) {
                minimumEffectEnchant = std::min(minimumEffectEnchant, enchanted);
            }
        }
    }

    const auto position = setItems.find(setItemId);
    fullSet = position != setItems.end() && position->second == setItem->m_nMaxPartsNum;
    if (!enchantValid || !fullSet) {
        minimumEnchant = 0;
        minimumEffectEnchant = 0;
    }
    const bool changed = m_data->m_NeedUpdatePacketBitSet.SetShortValue_IfModifed(
        reinterpret_cast<std::int16_t&>(m_hMinNewSetItemEnchantedEffect), static_cast<std::int16_t>(minimumEffectEnchant),
        0x4e
    );
    m_hMinNewSetItemEnchanted = static_cast<std::uint16_t>(minimumEnchant);
    if (changed && IsPC()) {
        CastUser()->ParamChanged(PCB_USER);
    }
}

void CCreature::GetEquipSlotNewSetItemInfo(std::map<int, int>& setItems) {
    // Current L2Server 0x006080f4.
    L2_GUARD_FUNCTION();
    int chestSetItemId = 0;
    CSPointer<CItem> chest = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_CHEST]);
    if (chest) {
        CItemConstInfo* const chestInfo = g_ItemConstInfoDB.GetItemConstInfo(chest->m_data->m_nClassID);
        if (chestInfo != nullptr) {
            std::vector<int> setIds;
            chestInfo->GetNewSetItemIDList(setIds);
            if (!setIds.empty()) {
                chestSetItemId = setIds.front();
            }
        }
    }

    for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
        if (slot == ST_LHAND || slot == ST_CHEST || slot == ST_ONEPIECE) {
            continue;
        }
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (!item) {
            continue;
        }
        CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->m_nClassID);
        if (itemInfo == nullptr) {
            continue;
        }
        std::vector<int> setIds;
        itemInfo->GetNewSetItemIDList(setIds);
        for (const int setItemId : setIds) {
            if (setItemId == chestSetItemId) {
                continue;
            }
            const auto position = setItems.find(setItemId);
            if (position == setItems.end()) {
                setItems.emplace(setItemId, 1);
            } else {
                ++position->second;
            }
        }
    }
}

std::map<int, int> CCreature::NewSet(bool& enchantedEffect) {
    // Current L2Server 0x0060bc60.
    L2_GUARD_FUNCTION();
    enchantedEffect = false;
    std::map<int, int> setItems;
    GetChestSlotNewSetItemInfo(setItems, enchantedEffect);
    GetEquipSlotNewSetItemInfo(setItems);
    return setItems;
}

void CCreature::DisableNewSetItemSkill() {
    // Current L2Server 0x00602498.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_NewSetItemSkillLock);
    for (const auto& entry : m_mapNewSetItem) {
        CNewSetItem* const item = g_ObjectDB.GetNewSetItemBySetItemId(entry.first);
        if (item != nullptr) {
            item->DeleteAllSkills(this);
        }
    }
    m_mapNewSetItem.clear();
}

void CCreature::NewSetItemSkillEnDisable(const std::map<int, int>& setItems, bool enchantedEffect) {
    // Current L2Server 0x00619d2c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_NewSetItemSkillLock);
    for (const auto& entry : m_mapNewSetItem) {
        CNewSetItem* const item = g_ObjectDB.GetNewSetItemBySetItemId(entry.first);
        if (item != nullptr) {
            item->DeleteAllSkills(this);
        }
    }
    for (const auto& entry : setItems) {
        if (entry.second <= 1) {
            continue;
        }
        CNewSetItem* const item = g_ObjectDB.GetNewSetItemBySetItemId(entry.first);
        if (item == nullptr) {
            continue;
        }
        if (item->m_pSkill != nullptr) {
            AcquireSkill(item->m_pSkill->m_nSkillId, entry.second - 1, 0, false, true);
        }
        if (entry.second == item->m_nMaxPartsNum) {
            CItem* const leftHand = m_inv.GetByIndex(m_data->m_nSlotIndex[ST_LHAND]);
            if (leftHand != nullptr &&
                std::find(item->m_vAdditionalList.begin(), item->m_vAdditionalList.end(), leftHand->m_data->m_nClassID) !=
                    item->m_vAdditionalList.end() &&
                item->m_pAdditionalSkill != nullptr) {
                AcquireSkill(item->m_pAdditionalSkill->m_nSkillId, 1, 0, false, true);
            }
            CSkillInfo* const enchantSkill = item->FindSetItemEnchantSkill(m_hMinNewSetItemEnchanted);
            if (enchantSkill != nullptr) {
                AcquireSkill(enchantSkill->m_nSkillId, enchantSkill->m_nLevel, 0, false, true);
            }
            if (enchantedEffect && item->m_pAdditional3Skill != nullptr) {
                AcquireSkill(item->m_pAdditional3Skill->m_nSkillId, 1, 0, false, true);
            }
        }
    }
    m_mapNewSetItem = setItems;
}

void CCreature::ValidateNewSetItem() {
    // Current L2Server 0x0061a090.
    L2_GUARD_FUNCTION();
    bool enchantedEffect = false;
    const std::map<int, int> setItems = NewSet(enchantedEffect);
    NewSetItemSkillEnDisable(setItems, enchantedEffect);
}

std::map<int, int> CCreature::ShapeShiftedSet(int* slotIndices) {
    // Current L2Server 0x0060bd44.
    L2_GUARD_FUNCTION();
    std::map<int, int> setItems;
    for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
        if (slot == ST_RLHAND || slot == ST_ONEPIECE || slot == ST_HAIR2 || slot == ST_HAIRALL) {
            continue;
        }
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(slotIndices[slot]);
        if (!item) {
            continue;
        }
        CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->GetShapeShiftedClassId());
        if (itemInfo == nullptr) {
            continue;
        }
        std::vector<int> setIds;
        itemInfo->GetShapeShiftedSetItemIDList(setIds);
        if (setIds.empty()) {
            continue;
        }
        for (const int setItemId : setIds) {
            static_cast<void>(g_ObjectDB.GetShapeShiftedSetItemBySetItemId(setItemId));
        }
        ++setItems[setIds.front()];
    }
    return setItems;
}

void CCreature::ShapeShiftedSetItemSkillEnDisable(std::map<int, int> setItems) {
    // Current L2Server 0x0061b2b0. The map is passed by value.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ShapeShiftedSetItemSkillLock);
    for (const auto& entry : m_mapShapeShiftedSetItem) {
        CShapeShiftedSetItem* const item = g_ObjectDB.GetShapeShiftedSetItemBySetItemId(entry.first);
        if (item != nullptr && item->m_pSkill != nullptr) {
            DeleteAcquireSkill(item->m_pSkill->m_nSkillId);
        }
    }
    for (const auto& entry : setItems) {
        if (entry.second <= 0) {
            continue;
        }
        CShapeShiftedSetItem* const item = g_ObjectDB.GetShapeShiftedSetItemBySetItemId(entry.first);
        if (item != nullptr && item->m_pSkill != nullptr) {
            AcquireSkill(item->m_pSkill->m_nSkillId, entry.second, item->m_pSkill->m_nSubLevel, false, true);
        }
    }
    m_mapShapeShiftedSetItem = setItems;
}

void CCreature::ValidateShapeShiftedSetItem() {
    // Current L2Server 0x0061ce88.
    L2_GUARD_FUNCTION();
    ShapeShiftedSetItemSkillEnDisable(ShapeShiftedSet(m_data->m_nSlotIndex));
}

SlotType CCreature::UnequipItem(SlotType slotType, bool force) {
    // Current L2Server 0x0061feec.
    L2_GUARD_FUNCTION();
    if (m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "data null at file[%s], line[%d]",
        //     "Creature_server.cpp",
        //     0x1472)
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Cannot unequip slot %d for object %u: shared creature data is missing.",
        //     static_cast<int>(slotType),
        //     m_nId)
        L2_LOG_ERROR(
            Log, "[equipment] Cannot unequip slot %d for object %u: shared creature data is missing.",
            static_cast<int>(slotType), m_nId
        );
        return ST_MAX;
    }
    if (static_cast<int>(slotType) > ST_MAX - 1) {
        return ST_MAX;
    }

    CItem* item;
    {
        L2_GUARD(*m_lock);
        if (slotType == ST_RHAND && m_data->m_nSlotIndex[ST_RLHAND] != -1) {
            m_data->m_nSlotIndex[ST_RLHAND] = -1;
            m_data->m_nSlotIndex[ST_RHAND] = -1;
            m_data->m_nSlotIndex[ST_LHAND] = -1;
        } else if (slotType == ST_CHEST && m_data->m_nSlotIndex[ST_ONEPIECE] != -1) {
            m_data->m_nSlotIndex[ST_ONEPIECE] = -1;
            m_data->m_nSlotIndex[ST_CHEST] = -1;
            m_data->m_nSlotIndex[ST_LEGS] = -1;
        } else if (slotType == ST_HAIR && m_data->m_nSlotIndex[ST_HAIRALL] != -1) {
            m_data->m_nSlotIndex[ST_HAIRALL] = -1;
            m_data->m_nSlotIndex[ST_HAIR] = -1;
            m_data->m_nSlotIndex[ST_HAIR2] = -1;
        } else {
            m_data->m_nSlotIndex[static_cast<int>(slotType)] = -1;
        }

        // Preserve the current ordering: the slot is read after it was cleared.
        item = m_inv.GetByIndex(m_data->m_nSlotIndex[static_cast<int>(slotType)]);
        if (item != nullptr) {
            _OnUnequipItem(item, slotType);
        }
        _ValidateSetItemOnUnequipItem(slotType, false);
        ValidateNewSetItem();
        ValidateShapeShiftedSetItem();
    }

    ValidateChanged(VB_MAX);
    if (!force) {
        m_inv.AddSlotInfo(-1, item);
    }
    return ST_REAR;
}

SlotType CCreature::UnequipItem(CItem* item, bool force) {
    // Current L2Server 0x00620144.
    L2_GUARD_FUNCTION();
    if (item == nullptr || !m_inv.Have(item->m_data->index)) {
        return ST_MAX;
    }

    SlotType slotType = ST_MAX;
    {
        L2_GUARD(*m_lock);
        for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
            if (m_data->m_nSlotIndex[slot] == item->m_data->index) {
                m_data->m_nSlotIndex[slot] = -1;
                slotType = static_cast<SlotType>(slot);
            }
        }
    }

    if (IsPC()) {
        item->ActivateUnequipSkillList(this, this);
    }
    {
        L2_GUARD(*m_lock);
        _OnUnequipItemProcess(item, slotType);
        _ValidateSetItemOnUnequipItem(slotType, true);
        ValidateNewSetItem();
        ValidateShapeShiftedSetItem();
        if (item->IsWeapon()) {
            m_ItemMod.m_bOnlyUseEquipedWeaponActiveSkill = false;
            m_ItemMod.m_bNoAttackWeapon = false;
            m_ItemMod.m_bHpLimitBreak = false;
        }
    }

    if (!force) {
        m_inv.AddSlotInfo(-1, item);
    }
    if (!IsPC()) {
        ValidateChanged(VB_MAX);
    }
    return slotType;
}

double CCreature::GetBonusAndTurnNextSoulShotState() {
    // Current L2Server 0x005ef124.
    L2_GUARD_FUNCTION();
    if (m_data->m_nSoulShot == 0) {
        return 0.0;
    }

    const double bonus = m_data->m_dSoulShotAttackBonusPer;
    if (m_sNowSoulShot == 1 || m_sNowSoulShot == 2) {
        m_sNowSoulShot = 2;
    } else {
        TurnOffSoulshot(false);
    }
    return bonus;
}

bool CCreature::IsGMInvisible() const { return m_data->m_bGMInvisible; }

void CCreature::CheckSummonBattleMode() {}

void CCreature::CheckSummonBattleTimeStamp() {}

bool CCreature::CanEquip(CItem*) { return false; }

void CCreature::TakeOffItems() {}

SlotType CCreature::equipItemWithoutDuplication(CItem* item, int firstSlot, int lastSlot) {
    // Current L2Server 0x005f15e8.
    L2_GUARD_FUNCTION();
    if (item == nullptr || static_cast<unsigned int>(firstSlot) >= ST_MAX || static_cast<unsigned int>(lastSlot) > ST_MAX) {
        return ST_NONE;
    }

    SlotType emptySlot = ST_NONE;
    for (int slot = firstSlot; slot < lastSlot; ++slot) {
        const int equippedIndex = m_data->m_nSlotIndex[slot];
        if (equippedIndex == -1) {
            if (emptySlot == ST_NONE) {
                emptySlot = static_cast<SlotType>(slot);
            }
            continue;
        }

        CItem* const equippedItem = m_inv.GetByIndex(equippedIndex);
        if (equippedItem == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid Item. User[%d]",
            //     L"Creature_server.cpp",
            //     0x65f6,
            //     GetName())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[equipment] Equip rejected for %s: slot %d refers to shared item index %d, which is absent from the
            //     inventory.", GetName(), slot, equippedIndex)
            // L2_LOG_ERROR(Log,
            //     L"[equipment] Equip rejected for %s: slot %d refers to shared item index %d, which is absent from the
            //     inventory.", GetName(), slot, equippedIndex)
            L2_LOG_ERROR(
                Log,
                u"[equipment] Equip rejected for %s: slot %d refers to shared item index %d, which is absent from the "
                u"inventory.",
                GetName(), slot, equippedIndex
            );
            return ST_NONE;
        }
        if (item->m_nClassId == equippedItem->m_nClassId) {
            if (IsPC()) {
                SendSystemMessage(SystemMessageId::SAME_ARTIFACT_ALREADY_EQUIPPED);
            }
            return ST_NONE;
        }
    }

    if (emptySlot != ST_NONE) {
        m_data->m_nSlotIndex[emptySlot] = item->m_data->index;
        return emptySlot;
    }
    if (IsPC()) {
        SendSystemMessage(SystemMessageId::NO_FREE_EQUIPMENT_SLOT);
    }
    return ST_NONE;
}

int CCreature::EquipItem(CItem* item, SlotType slotType) {
    // Current L2Server 0x00624e14.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item null at file[%s], line[%d]",
        //     "Creature_server.cpp",
        //     0x116b)
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Equip rejected for object %u, requested slot %d: the item pointer is null.",
        //     m_nId,
        //     static_cast<int>(slotType))
        L2_LOG_ERROR(
            Log, "[equipment] Equip rejected for object %u, requested slot %d: the item pointer is null.", m_nId,
            static_cast<int>(slotType)
        );
        return false;
    }
    if (!m_inv.Have(item->m_data->index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CCreature::EquipItem failed. no item",
        //     L"Creature_server.cpp",
        //     0x1171)
        // Log.Add(
        //     LOG_WARN,
        //     "[equipment] Equip rejected for object %u, requested slot %d: shared item index %d is absent from the
        //     inventory.", m_nId, static_cast<int>(slotType), item->m_data->index)
        L2_LOG_WARN(
            Log,
            "[equipment] Equip rejected for object %u, requested slot %d: shared item index %d is absent from the "
            "inventory.",
            m_nId, static_cast<int>(slotType), item->m_data->index
        );
        return false;
    }

    if (slotType > ST_ARTIFACT_D3) {
        slotType = ST_UNDERWEAR;
        while (slotType < ST_MAX && (ST2SBT[static_cast<int>(slotType)] & item->m_data->m_Slot) == 0) {
            slotType = static_cast<SlotType>(static_cast<int>(slotType) + 1);
        }
    }
    if (slotType < ST_UNDERWEAR || slotType >= ST_MAX || IsEquippedWithForcedItem(slotType)) {
        return false;
    }
    if (item->m_data->m_Type == IT_WEAPON && m_SkillDurationMod.m_nBlockEquipWeaponCount > 0) {
        return false;
    }
    if (item->m_data->m_Type == IT_ARMOR) {
        int firstSlot = 0;
        while (firstSlot < ST_MAX && (ST2SBT[firstSlot] & item->m_data->m_Slot) == 0) {
            ++firstSlot;
        }
        const bool chestArmor =
            firstSlot == ST_CHEST || firstSlot == ST_LEGS || firstSlot == ST_ONEPIECE || firstSlot == ST_ALLDRESS;
        if (chestArmor && m_SkillMod.m_nBlockEquipChestArmorCount > 0) {
            return false;
        }
    }

    if (IsPC() && item->m_const->m_AttackType == AT_FLAG) {
        User* const user = CastUser();
        CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
        CFortress* const fortress = pledge != nullptr ? pledge->m_pFortressUnderOffenceSiege : nullptr;
        if (fortress != nullptr && fortress->CanFlagBePicked(user, pledge, item, true)) {
            if (user->IsTransformed(TransformType::Max) != nullptr) {
                user->DispelBySlot(ABNS_TRANSFORM, nullptr, -1, nullptr, SPET_ETC, false);
            }
            if (CFortress::HandleFlagPicked(fortress, user, item)) {
                user->m_bHasFortressFlag = true;
            }
        }
    }

    std::map<int, CItem*> unequippedItems;
    bool succeeded = true;
    L2_GUARD(*m_lock);

    const auto save = [this, &unequippedItems](SlotType slot) {
        CreatureCommonFunc::SaveUnEquipedItemAndSetNull(this, unequippedItems, slot);
    };
    const auto setSlot = [this, item](SlotType slot) { m_data->m_nSlotIndex[static_cast<int>(slot)] = item->m_data->index; };
    const auto replaceSlot = [&save, &setSlot](SlotType slot) {
        save(slot);
        setSlot(slot);
    };

    if (slotType >= ST_HEAD && slotType <= ST_ONEPIECE && item->m_const->m_EtcitemType != EIT_LURE && slotType != ST_RHAND &&
        slotType != ST_LHAND && slotType != ST_RLHAND && slotType != ST_BACK) {
        if (m_data->m_nSlotIndex[ST_ALLDRESS] == m_data->m_nSlotIndex[ST_CHEST]) {
            save(ST_CHEST);
        }
        save(ST_ALLDRESS);
    }

    if (slotType < ST_RBRACELET) {
        if (slotType == ST_HAIRALL) {
            if (m_data->m_nSlotIndex[ST_HAIR] != -1) {
                save(ST_HAIR);
            }
            save(ST_HAIR);
            setSlot(ST_HAIR);
            save(ST_HAIR2);
            save(ST_HAIRALL);
            setSlot(ST_HAIRALL);
        } else if (slotType == ST_RLHAND) {
            save(ST_RHAND);
            setSlot(ST_RHAND);
            save(ST_LHAND);
            save(ST_RLHAND);
            setSlot(ST_RLHAND);
        } else if (slotType == ST_REAR || slotType == ST_LEAR) {
            if (m_data->m_nSlotIndex[ST_REAR] == -1) {
                slotType = ST_REAR;
                setSlot(slotType);
            } else if (m_data->m_nSlotIndex[ST_LEAR] == -1) {
                slotType = ST_LEAR;
                setSlot(slotType);
            } else {
                replaceSlot(slotType);
            }
        } else if (slotType == ST_RFINGER || slotType == ST_LFINGER) {
            if (m_data->m_nSlotIndex[ST_RFINGER] == -1) {
                slotType = ST_RFINGER;
                setSlot(slotType);
            } else if (m_data->m_nSlotIndex[ST_LFINGER] == -1) {
                slotType = ST_LFINGER;
                setSlot(slotType);
            } else {
                replaceSlot(slotType);
            }
        } else if (slotType == ST_RHAND || slotType == ST_LHAND) {
            const EtcItemType etcItemType = static_cast<EtcItemType>(item->m_const->m_EtcitemType);
            if (slotType == ST_LHAND && (etcItemType == EIT_LURE || etcItemType == EIT_ARROW || etcItemType == EIT_BOLT)) {
                CItem* const weapon = m_inv.GetByIndex(m_data->m_nSlotIndex[ST_RLHAND]);
                bool compatible = false;
                if (weapon != nullptr) {
                    const AttackType attackType = weapon->m_const->m_AttackType;
                    if (etcItemType == EIT_LURE) {
                        compatible = attackType == AT_FISHINGROD;
                    } else {
                        const bool correctWeapon = etcItemType == EIT_ARROW
                            ? attackType == AT_BOW
                            : attackType == AT_TWOHANDCROSSBOW || attackType == AT_CROSSBOW;
                        const int itemCrystal = static_cast<int>(item->m_const->GetCrystalType());
                        const int weaponCrystal = static_cast<int>(weapon->m_const->GetCrystalType());
                        compatible = correctWeapon && BaseCrystal[itemCrystal] == BaseCrystal[BaseCrystal[weaponCrystal]];
                    }
                }
                if (!compatible) {
                    succeeded = false;
                } else {
                    replaceSlot(ST_LHAND);
                    if (etcItemType == EIT_ARROW || etcItemType == EIT_BOLT) {
                        m_data->m_iLastArrowIndex = m_data->m_nSlotIndex[ST_LHAND];
                    }
                }
            } else {
                if (m_data->m_nSlotIndex[ST_RLHAND] == m_data->m_nSlotIndex[ST_RHAND]) {
                    save(ST_RHAND);
                }
                save(ST_RLHAND);
                replaceSlot(slotType);
            }
        } else if (slotType == ST_CHEST || slotType == ST_LEGS) {
            if (m_data->m_nSlotIndex[ST_ONEPIECE] == m_data->m_nSlotIndex[ST_CHEST]) {
                save(ST_CHEST);
            }
            save(ST_ONEPIECE);
            replaceSlot(slotType);
        } else if (slotType == ST_ONEPIECE) {
            save(ST_CHEST);
            setSlot(ST_CHEST);
            save(ST_LEGS);
            save(ST_ONEPIECE);
            setSlot(ST_ONEPIECE);
        } else if (slotType == ST_ALLDRESS) {
            for (int slot = ST_HEAD; slot < ST_ONEPIECE + 1; ++slot) {
                if (slot != ST_RHAND && slot != ST_LHAND && slot != ST_RLHAND) {
                    save(static_cast<SlotType>(slot));
                }
            }
            save(ST_CHEST);
            save(ST_ALLDRESS);
            setSlot(ST_CHEST);
            setSlot(ST_ALLDRESS);
        } else if (slotType == ST_HAIR || slotType == ST_HAIR2) {
            const int hairIndex = m_data->m_nSlotIndex[ST_HAIR];
            if (hairIndex == m_data->m_nSlotIndex[ST_HAIRALL] || (slotType == ST_HAIR && hairIndex != -1)) {
                save(ST_HAIR);
            }
            save(ST_HAIRALL);
            replaceSlot(slotType);
        } else {
            replaceSlot(slotType);
        }
    } else if (slotType == ST_RBRACELET || slotType == ST_LBRACELET) {
        replaceSlot(slotType);
    } else if (slotType >= ST_AGATHION_MAIN && slotType <= ST_AGATHION_SUB4) {
        if (m_data->m_nSlotIndex[ST_LBRACELET] == -1) {
            if (IsPC()) {
                CastUser()->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::CANNOT_USE_AGATHION_POWER_LEFT_BRACELET_MISSING, 1, 3,
                    item->m_data->m_nClassID
                );
            }
            return false;
        }

        int usedSubSlots = 0;
        for (int slot = ST_AGATHION_SUB1; slot <= ST_AGATHION_SUB4; ++slot) {
            if (m_data->m_nSlotIndex[slot] != -1) {
                ++usedSubSlots;
            }
        }
        const int maximumMain = m_SkillMod.m_dMaxAgathionMainSlotCount;
        const int maximumSub = m_SkillMod.m_dMaxAgathionSubSlotCount;
        if (maximumMain < 1) {
            if (maximumSub <= 0) {
                return false;
            }
            if (usedSubSlots >= maximumSub) {
                if (IsPC()) {
                    CastUser()->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_SUMMON_MORE_AGATHIONS, 1, 3, item->m_data->m_nClassID
                    );
                }
                return false;
            }
            for (int slot = ST_AGATHION_SUB1; slot <= ST_AGATHION_SUB4; ++slot) {
                if (m_data->m_nSlotIndex[slot] == -1) {
                    slotType = static_cast<SlotType>(slot);
                    setSlot(slotType);
                    break;
                }
            }
        } else if (maximumSub > 0 &&
            usedSubSlots + (m_data->m_nSlotIndex[ST_AGATHION_MAIN] != -1) < maximumMain + maximumSub) {
            for (int slot = ST_AGATHION_MAIN; slot <= ST_AGATHION_SUB4; ++slot) {
                if (m_data->m_nSlotIndex[slot] == -1) {
                    slotType = static_cast<SlotType>(slot);
                    setSlot(slotType);
                    break;
                }
            }
        } else {
            slotType = ST_AGATHION_MAIN;
            replaceSlot(slotType);
        }
    } else if (slotType >= ST_DECO1 && slotType <= ST_DECO6) {
        if (m_data->m_nSlotIndex[ST_RBRACELET] == -1) {
            if (IsPC()) {
                CastUser()->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_BRACELET_MISSING, 1, 3, item->m_data->m_nClassID
                );
            }
            return false;
        }
        int used = 0;
        for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
            used += m_data->m_nSlotIndex[slot] != -1;
        }
        if (m_SkillMod.m_dMaxDecoSlotCount <= used && IsPC()) {
            CastUser()->GetSocket()->Send(
                "chccd", 0x62, SystemMessageId::NO_FREE_SLOT_FOR_NAMED_ITEM, 1, 3, item->m_data->m_nClassID
            );
            return false;
        }
        SlotType empty = ST_NONE;
        for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
            if (m_data->m_nSlotIndex[slot] == -1) {
                empty = static_cast<SlotType>(slot);
                break;
            }
        }
        slotType = empty != ST_NONE ? empty : ST_DECO1;
        if (empty == ST_NONE) {
            save(slotType);
        }
        setSlot(slotType);
    } else if (slotType == ST_WAIST || slotType == ST_BROOCH) {
        if (IsPC()) {
            User* const inventoryLimitUser = CastUser();
            User* const beltLimitUser = CastUser();
            User* const inventoryUser = CastUser();
            const int totalCount = inventoryUser->m_inv.GetTotalCount();
            const int beltLimit = beltLimitUser->GetBeltInventoryLimit();
            const int inventoryLimit = inventoryLimitUser->GetInventoryLimit();
            if (inventoryLimit - beltLimit < totalCount) {
                CastUser()->SendSystemMessage(
                    SystemMessageId::CANNOT_CHANGE_BELT_OR_BROOCH_WITH_EXPANDED_INVENTORY_OCCUPIED
                );
                return false;
            }
        }
        replaceSlot(slotType);
    } else if (slotType >= ST_JEWEL1 && slotType <= ST_JEWEL6) {
        if (m_data->m_nSlotIndex[ST_BROOCH] == -1) {
            if (IsPC()) {
                CastUser()->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_BROOCH_MISSING, 1, 3, item->m_data->m_nClassID
                );
            }
            return false;
        }
        int used = 0;
        for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
            used += m_data->m_nSlotIndex[slot] != -1;
        }
        if (m_SkillMod.m_dMaxJewelSlotCount <= used && IsPC()) {
            CastUser()->GetSocket()->Send(
                "chccd", 0x62, SystemMessageId::NO_FREE_EQUIPMENT_SLOT, 1, 3, item->m_data->m_nClassID
            );
            return false;
        }
        SlotType empty = ST_NONE;
        for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
            if (m_data->m_nSlotIndex[slot] == -1) {
                empty = static_cast<SlotType>(slot);
                break;
            }
        }
        slotType = empty != ST_NONE ? empty : ST_JEWEL1;
        if (empty == ST_NONE) {
            save(slotType);
        }
        setSlot(slotType);
    } else if (slotType == ST_ARTIFACTBOOK) {
        replaceSlot(slotType);
    } else if (slotType >= ST_ARTIFACT_A1 && slotType <= ST_ARTIFACT_D3) {
        if (slotType <= ST_ARTIFACT_A12) {
            if (m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                if (IsPC()) {
                    CastUser()->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_ARTIFACT_BOOK_MISSING, 1, 3,
                        item->m_data->m_nClassID
                    );
                }
                return false;
            }
            const int activeGroups = m_SkillMod.m_nActiveArtifactSlotGroup;
            int activeSlotCount = runtime::MultiplyInt32Wrapping(activeGroups, 4);
            if (activeSlotCount < 0 || activeSlotCount > 12) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Invalid number of active artifact slots. "
                //     L"nActiveArtifactSlotGroup[%d]",
                //     L"Creature_server.cpp",
                //     0x13a3,
                //     activeGroups)
                // Log.Add(
                //     LOG_ERROR,
                //     "[equipment] Invalid artifact A slot count for object %u: %d active groups produced %d slots (allowed
                //     0..12), using zero slots for this equip attempt.", m_nId, activeGroups, activeSlotCount)
                L2_LOG_ERROR(
                    Log,
                    "[equipment] Invalid artifact A slot count for object %u: %d active groups produced %d slots (allowed "
                    "0..12); using zero slots for this equip attempt.",
                    m_nId, activeGroups, activeSlotCount
                );
                activeSlotCount = 0;
            }
            slotType = equipItemWithoutDuplication(item, ST_ARTIFACT_A1, ST_ARTIFACT_A1 + activeSlotCount);
        } else if (slotType <= ST_ARTIFACT_B3) {
            if (m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                if (IsPC()) {
                    CastUser()->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_ARTIFACT_BOOK_MISSING, 1, 3,
                        item->m_data->m_nClassID
                    );
                }
                return false;
            }
            const int activeGroups = m_SkillMod.m_nActiveArtifactSlotGroup;
            int activeSlotCount = activeGroups;
            if (activeSlotCount < 0 || activeSlotCount > 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Invalid number of active artifact slots. "
                //     L"nActiveArtifactSlotGroup[%d]",
                //     L"Creature_server.cpp",
                //     0x13c3,
                //     activeGroups)
                // Log.Add(
                //     LOG_ERROR,
                //     "[equipment] Invalid artifact B slot count for object %u: %d active groups produced %d slots (allowed
                //     0..3), using zero slots for this equip attempt.", m_nId, activeGroups, activeSlotCount)
                L2_LOG_ERROR(
                    Log,
                    "[equipment] Invalid artifact B slot count for object %u: %d active groups produced %d slots (allowed "
                    "0..3); using zero slots for this equip attempt.",
                    m_nId, activeGroups, activeSlotCount
                );
                activeSlotCount = 0;
            }
            slotType = equipItemWithoutDuplication(item, ST_ARTIFACT_B1, ST_ARTIFACT_B1 + activeSlotCount);
        } else if (slotType <= ST_ARTIFACT_C3) {
            if (m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                if (IsPC()) {
                    CastUser()->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_ARTIFACT_BOOK_MISSING, 1, 3,
                        item->m_data->m_nClassID
                    );
                }
                return false;
            }
            const int activeGroups = m_SkillMod.m_nActiveArtifactSlotGroup;
            int activeSlotCount = activeGroups;
            if (activeSlotCount < 0 || activeSlotCount > 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Invalid number of active artifact slots. "
                //     L"nActiveArtifactSlotGroup[%d]",
                //     L"Creature_server.cpp",
                //     0x13e3,
                //     activeGroups)
                // Log.Add(
                //     LOG_ERROR,
                //     "[equipment] Invalid artifact C slot count for object %u: %d active groups produced %d slots (allowed
                //     0..3), using zero slots for this equip attempt.", m_nId, activeGroups, activeSlotCount)
                L2_LOG_ERROR(
                    Log,
                    "[equipment] Invalid artifact C slot count for object %u: %d active groups produced %d slots (allowed "
                    "0..3); using zero slots for this equip attempt.",
                    m_nId, activeGroups, activeSlotCount
                );
                activeSlotCount = 0;
            }
            slotType = equipItemWithoutDuplication(item, ST_ARTIFACT_C1, ST_ARTIFACT_C1 + activeSlotCount);
        } else {
            if (m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                if (IsPC()) {
                    CastUser()->GetSocket()->Send(
                        "chccd", 0x62, SystemMessageId::CANNOT_EQUIP_ITEM_ARTIFACT_BOOK_MISSING, 1, 3,
                        item->m_data->m_nClassID
                    );
                }
                return false;
            }
            const int activeGroups = m_SkillMod.m_nActiveArtifactSlotGroup;
            int activeSlotCount = activeGroups;
            if (activeSlotCount < 0 || activeSlotCount > 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]Invalid number of active artifact slots. "
                //     L"nActiveArtifactSlotGroup[%d]",
                //     L"Creature_server.cpp",
                //     0x1403,
                //     activeGroups)
                // Log.Add(
                //     LOG_ERROR,
                //     "[equipment] Invalid artifact D slot count for object %u: %d active groups produced %d slots (allowed
                //     0..3), using zero slots for this equip attempt.", m_nId, activeGroups, activeSlotCount)
                L2_LOG_ERROR(
                    Log,
                    "[equipment] Invalid artifact D slot count for object %u: %d active groups produced %d slots (allowed "
                    "0..3); using zero slots for this equip attempt.",
                    m_nId, activeGroups, activeSlotCount
                );
                activeSlotCount = 0;
            }
            slotType = equipItemWithoutDuplication(item, ST_ARTIFACT_D1, ST_ARTIFACT_D1 + activeSlotCount);
        }
        if (slotType == ST_NONE) {
            return false;
        }
    } else {
        replaceSlot(slotType);
    }

    if (!succeeded) {
        return false;
    }

    for (const auto& entry : unequippedItems) {
        if (entry.second != nullptr) {
            _OnUnequipItem(entry.second, slotType);
        }
    }
    _OnEquipItem(item, slotType);
    _ValidateSetItemOnEquipItem(item, slotType, false);
    ValidateNewSetItem();
    ValidateShapeShiftedSetItem();
    if (item->IsWeapon()) {
        m_ItemMod.m_bNoAttackWeapon = item->m_const->m_bNoAttackWeapon;
        m_ItemMod.m_bOnlyUseEquipedWeaponActiveSkill = item->m_const->m_bOnlyUseEquipedWeaponActiveSkill;
        m_ItemMod.m_bHpLimitBreak = item->m_const->m_bHpLimitBreak;
    }
    m_inv.RemoveSlotInfo(item);
    if (IsPC()) {
        ParamChanged(PCB_USER | PCB_CHAR_EQUIPSLOT);
    }
    return true;
}

void CCreature::_OnSwapAgathionSlotItems(CItem*, ::SlotType, CItem*, ::SlotType) {}

bool CCreature::SwapAgathionSlotItems(CItem* first, ::SlotType firstSlot, CItem* second, ::SlotType secondSlot) {
    L2_GUARD_FUNCTION();
    if (first == nullptr || second == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CCreature::SwapAgathionSlotItems failed. "
        //     "Item nullptr",
        //     "Creature_server.cpp",
        //     0x151e)
        // Log.Add(
        //     LOG_ERROR,
        //     "[equipment] Agathion swap rejected for object %u: item pointers present, first %s, second %s.",
        //     m_nId,
        //     first != nullptr ? "yes" : "no",
        //     second != nullptr ? "yes" : "no")
        L2_LOG_ERROR(
            Log, "[equipment] Agathion swap rejected for object %u: item pointers present, first %s, second %s.", m_nId,
            first != nullptr ? "yes" : "no", second != nullptr ? "yes" : "no"
        );
        return false;
    }

    const bool haveFirst = m_inv.Have(first->m_data->index);
    const bool haveSecond = m_inv.Have(second->m_data->index);
    if (!haveFirst || !haveSecond) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CCreature::SwapAgathionSlotItems failed. "
        //     "No Item",
        //     "Creature_server.cpp",
        //     0x1526)
        // Log.Add(
        //     LOG_WARN,
        //     "[equipment] Agathion swap rejected for object %u: inventory membership, first %s, second %s.",
        //     m_nId,
        //     haveFirst ? "yes" : "no",
        //     haveSecond ? "yes" : "no")
        L2_LOG_WARN(
            Log, "[equipment] Agathion swap rejected for object %u: inventory membership, first %s, second %s.", m_nId,
            haveFirst ? "yes" : "no", haveSecond ? "yes" : "no"
        );
        return false;
    }

    constexpr std::int64_t kAgathionSlotBits = 0x0000003000000000LL;
    const bool validFirstSlotBits = (first->m_data->m_Slot & kAgathionSlotBits) != 0;
    const bool validSecondSlotBits = (second->m_data->m_Slot & kAgathionSlotBits) != 0;
    if (!validFirstSlotBits || !validSecondSlotBits) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CCreature::SwapAgathionSlotItems failed. "
        //     "Invalid SlotBitType",
        //     "Creature_server.cpp",
        //     0x1530)
        // Log.Add(
        //     LOG_WARN,
        //     "[equipment] Agathion swap rejected for object %u: compatible item slot masks, first %s, second %s.",
        //     m_nId,
        //     validFirstSlotBits ? "yes" : "no",
        //     validSecondSlotBits ? "yes" : "no")
        L2_LOG_WARN(
            Log, "[equipment] Agathion swap rejected for object %u: compatible item slot masks, first %s, second %s.", m_nId,
            validFirstSlotBits ? "yes" : "no", validSecondSlotBits ? "yes" : "no"
        );
        return false;
    }

    if (firstSlot < ST_AGATHION_MAIN || firstSlot > ST_AGATHION_SUB4 || secondSlot < ST_AGATHION_MAIN ||
        secondSlot > ST_AGATHION_SUB4) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CCreature::SwapAgathionSlotItems failed. "
        //     "Invalid SlotType",
        //     "Creature_server.cpp",
        //     0x153a)
        // Log.Add(
        //     LOG_WARN,
        //     "[equipment] Agathion swap rejected for object %u: requested slots %d and %d must both be in %d..%d.",
        //     m_nId,
        //     static_cast<int>(firstSlot),
        //     static_cast<int>(secondSlot),
        //     static_cast<int>(ST_AGATHION_MAIN),
        //     static_cast<int>(ST_AGATHION_SUB4))
        L2_LOG_WARN(
            Log, "[equipment] Agathion swap rejected for object %u: requested slots %d and %d must both be in %d..%d.",
            m_nId, static_cast<int>(firstSlot), static_cast<int>(secondSlot), static_cast<int>(ST_AGATHION_MAIN),
            static_cast<int>(ST_AGATHION_SUB4)
        );
        return false;
    }

    {
        L2_GUARD(*m_lock);
        const int secondIndex = second->m_data->index;
        if (m_data->m_nSlotIndex[firstSlot] != first->m_data->index || m_data->m_nSlotIndex[secondSlot] != secondIndex) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] CCreature::SwapAgathionSlotItems failed. "
            //     "Invalid SlotIndex",
            //     "Creature_server.cpp",
            //     0x1547)
            // Log.Add(
            //     LOG_WARN,
            //     "[equipment] Agathion swap rejected for object %u: slot %d contains shared index %d, expected %d. Slot %d
            //     contains shared index %d, expected %d.", m_nId, static_cast<int>(firstSlot),
            //     m_data->m_nSlotIndex[firstSlot],
            //     first->m_data->index,
            //     static_cast<int>(secondSlot),
            //     m_data->m_nSlotIndex[secondSlot],
            //     secondIndex)
            L2_LOG_WARN(
                Log,
                "[equipment] Agathion swap rejected for object %u: slot %d contains shared index %d, expected %d; slot %d "
                "contains shared index %d, expected %d.",
                m_nId, static_cast<int>(firstSlot), m_data->m_nSlotIndex[firstSlot], first->m_data->index,
                static_cast<int>(secondSlot), m_data->m_nSlotIndex[secondSlot], secondIndex
            );
            return false;
        }

        m_data->m_nSlotIndex[firstSlot] = secondIndex;
        m_data->m_nSlotIndex[secondSlot] = first->m_data->index;
    }

    if (IsPC()) {
        first->ActivateUnequipSkillList(this, this);
        second->ActivateUnequipSkillList(this, this);
    }

    {
        L2_GUARD(*m_lock);
        _OnSwapAgathionSlotItems(first, firstSlot, second, secondSlot);
        if (IsPC()) {
            ParamChanged(PCB_USER | PCB_CHAR_EQUIPSLOT);
        }
    }
    return true;
}

void CCreature::UnequipWeapon(bool) {}

void CCreature::RestoreUnequipedWeapon() {}

void CCreature::UnequipArmor(::SlotType) {}

void CCreature::RestoreUnequipedArmor(::SlotType) {}

int CCreature::GetInventoryLimit() { return 0; }

int CCreature::GetQuestInventoryLimit() { return 0; }

int CCreature::GetArtifactInventoryLimit() { return 0; }

bool CCreature::StopStore(bool, bool) { return false; }

bool CCreature::CancelStore() { return false; }

int CCreature::GetPrivateStoreLimit() { return 0; }

int CCreature::GetPrivateStoreBuyLimit() { return 0; }

int CCreature::GetPrivateBankLimit() { return 0; }

void CCreature::_OnEquipItem(CItem*, ::SlotType) {}

void CCreature::_OnUnequipItemProcess(CItem*, ::SlotType) {}

void CCreature::_OnUnequipItem(CItem*, ::SlotType) {}

void CCreature::SendSystemMessage(const char16_t*, const char16_t*) {}

void CCreature::SendSystemMessage(unsigned int) {}

void CCreature::SendSystemMessage_d(unsigned int, int) {}

void CCreature::SendSystemMessage_Q(unsigned int, std::int64_t) {}

void CCreature::SendSystemMessageWithSelf(unsigned int) {}

bool CCreature::AddItemToInventory3(
    const int itemClassId, const int count, const bool reject, int, int, int, ItemOptionKey, int, BaseAttribute
) {
    // Current L2Server 0x005ffa20.
    L2_GUARD_FUNCTION();
    if (!IsNPC() || count == 0 || reject) {
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"Creature_server.cpp",
        //     0x34a8,
        //     itemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Cannot add fake item class %d to object %u: the prototype is missing or is not an item.",
        //     itemClassId,
        //     m_nId)
        L2_LOG_ERROR(
            Log, "[inventory] Cannot add fake item class %d to object %u: the prototype is missing or is not an item.",
            itemClassId, m_nId
        );
        return false;
    }

    CItem* const prototype = object->CastItem();
    if (prototype == nullptr || !prototype->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"Creature_server.cpp",
        //     0x34b0,
        //     itemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Cannot add fake item class %d to object %u: the item cast returned null or a non-item.",
        //     itemClassId,
        //     m_nId)
        L2_LOG_ERROR(
            Log, "[inventory] Cannot add fake item class %d to object %u: the item cast returned null or a non-item.",
            itemClassId, m_nId
        );
        return false;
    }
    prototype->SetDBID(0);

    CItemFactory& items = CItemFactory::Inst();
    CSharedItemData* itemData = m_inv.GetFirst();
    while (itemData != nullptr) {
        if (count < 1) {
            return true;
        }
        CSharedItemData* const next = items.GetNext(itemData);
        CSPointer<CItem> item = items.FindObjectSP(itemData->index);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]ERROR, Cannot Find item object. item classid[%d][%s]",
            //     L"Creature_server.cpp",
            //     0x34c0,
            //     itemData->m_nClassID,
            //     GetName())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Skipping item class %d for %s during the fake-item duplicate scan: shared index %d has no
            //     live object, continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
            // L2_LOG_ERROR(Log,
            //     L"[inventory] Skipping item class %d for %s during the fake-item duplicate scan: shared index %d has no
            //     live object, continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Skipping item class %d for %s during the fake-item duplicate scan: shared index %d has no "
                u"live object; continuing the scan.",
                itemData->m_nClassID, GetName(), itemData->index
            );
        } else if (itemData->m_nClassID == itemClassId) {
            return false;
        }
        itemData = next;
    }

    if (count > 0) {
        CItem* const item = prototype->CopyFakeItem();
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateItem Failed. item class[%d] nCount[%d]",
            //     L"Creature_server.cpp",
            //     0x34e7,
            //     itemClassId,
            //     count)
            // Log.Add(
            //     LOG_ERROR,
            //     "[inventory] Fake-item creation failed for object %u: item class %d, requested count %d.",
            //     m_nId,
            //     itemClassId,
            //     count)
            L2_LOG_ERROR(
                Log, "[inventory] Fake-item creation failed for object %u: item class %d, requested count %d.", m_nId,
                itemClassId, count
            );
        } else if (!item->SetAmount(static_cast<std::int64_t>(count))) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%d][%d]",
            //     "Creature_server.cpp",
            //     0x34d4,
            //     static_cast<long long>(item->m_data->m_nCount),
            //     static_cast<long long>(count))
            // Log.Add(
            //     LOG_ERROR,
            //     "[inventory] Fake-item amount assignment rejected for object %u, item class %d: observed count %lld,
            //     requested count %lld, inventory insertion skipped.", m_nId, itemClassId, static_cast<long
            //     long>(item->m_data->m_nCount), static_cast<long long>(count))
            L2_LOG_ERROR(
                Log,
                "[inventory] Fake-item amount assignment rejected for object %u, item class %d: observed count %lld, "
                "requested count %lld; inventory insertion skipped.",
                m_nId, itemClassId, static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(count)
            );
        } else {
            if (item->IsDuration()) {
                item->SetInitialDuration();
            }
            item->m_data->m_nInventorySlotIndex = -1;
            if (!m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "Creature_server.cpp",
                //     0x34e1)
                // Log.Add(
                //     LOG_ERROR,
                //     "[inventory] Could not insert fake item class %d, shared index %d into object %u inventory (requested
                //     count %d).", itemClassId, item->m_data->index, m_nId, count)
                L2_LOG_ERROR(
                    Log,
                    "[inventory] Could not insert fake item class %d, shared index %d into object %u inventory (requested "
                    "count %d).",
                    itemClassId, item->m_data->index, m_nId, count
                );
            }
        }
    }
    return true;
}

bool CCreature::AddItemToInventory(
    int itemClassId,
    std::int64_t count,
    ItemGetReason::Enum reason,
    int enchanted,
    int blessed,
    int eroded,
    ItemOptionKey optionKey,
    int intensiveItemClassId,
    BaseAttribute attribute
) {
    // Current L2Server 0x0060e704.
    L2_GUARD_FUNCTION();
    InventoryParams parameters;
    parameters.nItemClassID = itemClassId;
    parameters.nCount = count;
    parameters.m_Reason = reason;
    parameters.nEnchanted = enchanted;
    parameters.nBless = blessed;
    parameters.nEroded = eroded;
    parameters.key = optionKey;
    parameters.nIntensiveItemClassID = intensiveItemClassId;
    parameters.attribute = attribute;
    return AddItemToInventory(parameters);
}

bool CCreature::AddItemToInventory(InventoryParams& parameters) {
    // Current L2Server 0x005ffdd4.
    L2_GUARD_FUNCTION();
    const std::int64_t requestedCount = parameters.nCount;
    if (requestedCount == 0) {
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(parameters.nItemClassID, false);
    CItem* const prototype = object != nullptr && object->IsItem() ? static_cast<CItem*>(object) : nullptr;
    if (prototype == nullptr) {
        if (IsPC()) {
            User* const user = CastUser();
            runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"Creature_server.cpp",
        //     0x3522,
        //     parameters.nItemClassID)
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Cannot add item class %d to object %u: the prototype is missing or is not an item (requested
        //     count %lld).", parameters.nItemClassID, m_nId, static_cast<long long>(requestedCount))
        L2_LOG_ERROR(
            Log,
            "[inventory] Cannot add item class %d to object %u: the prototype is missing or is not an item (requested count "
            "%lld).",
            parameters.nItemClassID, m_nId, static_cast<long long>(requestedCount)
        );
        return false;
    }
    prototype->SetDBID(0);

    if (IsPC()) {
        User* const user = CastUser();
        if (!user->m_inv.CheckAddable(parameters.nItemClassID, requestedCount, false)) {
            SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
            User* const failureUser = CastUser();
            runtime::raw_atomic::ExchangeI32(&failureUser->m_nWaitGetItemOnce, 0);
            return false;
        }

        std::array<char, 20480> packet;
        int packetSize = 0;
        int itemCount = 0;
        if (CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
            if (MsgHelper_ManipulateItem(
                    packet.data(), static_cast<int>(packet.size()), packetSize, prototype, parameters
                ) != 0) {
                itemCount = 1;
            }
        } else {
            parameters.nCount = 1;
            int index = 0;
            while (static_cast<std::int64_t>(index) < requestedCount) {
                if (MsgHelper_ManipulateItem(
                        packet.data(), static_cast<int>(packet.size()), packetSize, prototype, parameters
                    ) != 0) {
                    ++itemCount;
                }
                index = runtime::AddInt32Wrapping(index, 1);
            }
        }

        db.CreateUpdateItems(user, itemCount, packetSize, packet.data(), nullptr, 0, nullptr, false);
        return true;
    }

    L2_WRITE_GUARD(m_inv);
    if (!CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
        for (std::int64_t remaining = requestedCount; remaining > 0; --remaining) {
            CItem* const item = g_ObjectDB.CreateItem(parameters.nItemClassID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CreateItem Failed. item class[%d] "
                //     L"nCount[%lld]",
                //     L"Creature_server.cpp",
                //     0x3594,
                //     parameters.nItemClassID,
                //     static_cast<long long>(remaining))
                // Log.Add(
                //     LOG_ERROR,
                //     "[inventory] Could not create one non-stackable item for object %u: class %d, %lld attempts remaining
                //     including this one, skipping this attempt.", m_nId, parameters.nItemClassID, static_cast<long
                //     long>(remaining))
                L2_LOG_ERROR(
                    Log,
                    "[inventory] Could not create one non-stackable item for object %u: class %d, %lld attempts remaining "
                    "including this one; skipping this attempt.",
                    m_nId, parameters.nItemClassID, static_cast<long long>(remaining)
                );
                continue;
            }
            if (IsNPC() && !m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "Creature_server.cpp",
                //     0x359c)
                // Log.Add(
                //     LOG_ERROR,
                //     "[inventory] Could not insert non-stackable item class %d, shared index %d into object %u inventory.
                //     continuing the creation loop.", parameters.nItemClassID, item->m_data->index, m_nId)
                L2_LOG_ERROR(
                    Log,
                    "[inventory] Could not insert non-stackable item class %d, shared index %d into object %u inventory; "
                    "continuing the creation loop.",
                    parameters.nItemClassID, item->m_data->index, m_nId
                );
            }
        }
        return true;
    }

    std::int64_t remaining = requestedCount;
    for (CSharedItemData* itemData = m_inv.GetFirst(); itemData != nullptr && remaining > 0;) {
        CSharedItemData* const nextItemData = itemData->Next();
        {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!item) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]ERROR, Cannot Find item object. "
                //     L"item classid[%d][%s]",
                //     L"Creature_server.cpp",
                //     0x3563,
                //     itemData->m_nClassID,
                //     GetName())
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Skipping item class %d for %s during stack addition: shared index %d has no live object.
                //     continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
                // L2_LOG_ERROR(Log,
                //     L"[inventory] Skipping item class %d for %s during stack addition: shared index %d has no live object.
                //     continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Skipping item class %d for %s during stack addition: shared index %d has no live object; "
                    u"continuing the scan.",
                    itemData->m_nClassID, GetName(), itemData->index
                );
            } else if (itemData->m_nClassID == parameters.nItemClassID && IsNPC()) {
                {
                    L2_GUARD(*item->m_lock);
                    itemData->m_nCount += remaining;
                }
                remaining = 0;
            }
        }
        itemData = nextItemData;
    }

    if (remaining > 0) {
        CItem* const item = g_ObjectDB.CreateItem(parameters.nItemClassID);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateItem Failed. item class[%d] "
            //     L"nCount[%lld]",
            //     L"Creature_server.cpp",
            //     0x3587,
            //     parameters.nItemClassID,
            //     static_cast<long long>(remaining))
            // Log.Add(
            //     LOG_ERROR,
            //     "[inventory] Could not create a new stack for object %u: item class %d, remaining count %lld.",
            //     m_nId,
            //     parameters.nItemClassID,
            //     static_cast<long long>(remaining))
            L2_LOG_ERROR(
                Log, "[inventory] Could not create a new stack for object %u: item class %d, remaining count %lld.", m_nId,
                parameters.nItemClassID, static_cast<long long>(remaining)
            );
        } else {
            item->m_data->m_nCount = remaining;
            if (IsNPC() && !m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "Creature_server.cpp",
                //     0x3581)
                // Log.Add(
                //     LOG_ERROR,
                //     "[inventory] Could not insert the new stack into object %u inventory: item class %d, shared index %d,
                //     assigned count %lld.", m_nId, parameters.nItemClassID, item->m_data->index, static_cast<long
                //     long>(remaining))
                L2_LOG_ERROR(
                    Log,
                    "[inventory] Could not insert the new stack into object %u inventory: item class %d, shared index %d, "
                    "assigned count %lld.",
                    m_nId, parameters.nItemClassID, item->m_data->index, static_cast<long long>(remaining)
                );
            }
        }
    }
    return true;
}

bool CCreature::AddItemToInventory2(CItem*) { return false; }

bool CCreature::DeleteItemInInventory(int) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CCreature::DeleteItemInInventoryBeforeCommit(const int itemClassId, const std::int64_t count) {
    // Current L2Server 0x005fa978.
    L2_GUARD_FUNCTION();
    if (count < 1) {
        return false;
    }

    CItem* const item = m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr) {
        return false;
    }

    L2_WRITE_GUARD(m_inv);
    if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        if (count == 1) {
            m_inv.Pop(item->m_data->index, false);
            item->Delete();
            return true;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot delete non-stackable item in inventory. "
        //     L"count[%I64d] item[%d]",
        //     L"Creature_server.cpp",
        //     0x364a,
        //     static_cast<long long>(count),
        //     static_cast<int>(item->Id()))
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Removal before commit rejected for object %u: requested count %lld for non-stackable item object
        //     %u, this operation requires exactly one.", m_nId, static_cast<long long>(count), item->Id())
        L2_LOG_ERROR(
            Log,
            "[inventory] Removal before commit rejected for object %u: requested count %lld for non-stackable item object "
            "%u; this operation requires exactly one.",
            m_nId, static_cast<long long>(count), item->Id()
        );
        return false;
    }

    if (!m_inv.Have(item->m_data->index)) {
        return false;
    }

    std::int64_t realDelete;
    {
        L2_GUARD(*item->m_lock);
        realDelete = std::min(count, item->m_data->m_nCount);
        if (!item->AddAmount(-realDelete)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
            //     "Creature_server.cpp",
            //     0x3632,
            //     static_cast<long long>(item->m_data->m_nCount),
            //     static_cast<long long>(-realDelete))
            // Log.Add(
            //     LOG_ERROR,
            //     "[inventory] Amount adjustment before commit rejected for object %u, item class %d: observed count %lld,
            //     attempted delta %lld, continuing removal cleanup.", m_nId, itemClassId, static_cast<long
            //     long>(item->m_data->m_nCount), static_cast<long long>(-realDelete))
            L2_LOG_ERROR(
                Log,
                "[inventory] Amount adjustment before commit rejected for object %u, item class %d: observed count %lld, "
                "attempted delta %lld; continuing removal cleanup.",
                m_nId, itemClassId, static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(-realDelete)
            );
        }
    }

    if (count - realDelete < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]DeleteItemInInventoryBeforeCommit error, "
        //     L"req del count[%I64d], remain item count[%I64d], "
        //     L"real del count[%I64d]",
        //     "Creature_server.cpp",
        //     0x3638,
        //     static_cast<long long>(count - realDelete),
        //     static_cast<long long>(item->m_data->m_nCount),
        //     static_cast<long long>(realDelete))
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Negative unfulfilled count %lld after the removal attempt for object %u, item class %d: observed
        //     item count %lld, selected removal count %lld, continuing cleanup.", static_cast<long long>(count -
        //     realDelete), m_nId, itemClassId, static_cast<long long>(item->m_data->m_nCount), static_cast<long
        //     long>(realDelete))
        L2_LOG_ERROR(
            Log,
            "[inventory] Negative unfulfilled count %lld after the removal attempt for object %u, item class %d: observed "
            "item count %lld, selected removal count %lld; continuing cleanup.",
            static_cast<long long>(count - realDelete), m_nId, itemClassId, static_cast<long long>(item->m_data->m_nCount),
            static_cast<long long>(realDelete)
        );
    }
    if (item->m_data->m_nCount < 1) {
        m_inv.Pop(item->m_data->index, false);
        item->Delete();
    }
    return true;
}

void CCreature::DeleteAllItemsInInventory() {
    // Current L2Server 0x005fead0.
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_inv);

    CSharedItemData* current = m_inv.GetFirst();
    int depth = 0;
    while (current != nullptr) {
        if (current == nullptr) {
            // Noise: This null check is inside while (current != nullptr), with no intervening assignment or call. The
            // diagnostic cannot be reached. The original check and return remain. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] item in invenroty is null.",
            //     L"Creature_server.cpp",
            //     0x26f)
            return;
        }

        CSharedItemData* const next = current->Next();
        ++depth;
        if (depth > 0x192) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"depth > %d at file[%s], line[%d]",
            //     0x192,
            //     L"Creature_server.cpp",
            //     0x27a)
            // Log.Add(
            //     LOG_ERROR,
            //     "[inventory] Clearing inventory stopped for object %u at entry %d: traversal limit %d reached. Earlier
            //     removals remain and equipment-slot reset was not reached.", m_nId, depth, 0x192)
            L2_LOG_ERROR(
                Log,
                "[inventory] Clearing inventory stopped for object %u at entry %d: traversal limit %d reached; earlier "
                "removals remain and equipment-slot reset was not reached.",
                m_nId, depth, 0x192
            );
            return;
        }

        const int itemIndex = current->index;
        m_inv.Pop(itemIndex, true);
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (item) {
            item->Delete();
        }
        current = next;
    }

    for (std::int32_t& slotIndex : m_data->m_nSlotIndex) {
        slotIndex = -1;
    }
}

bool CCreature::DeleteItemInInventory(const int itemClassId, const std::int64_t count, ::AtomicJob*, bool, bool) {
    // Current L2Server 0x00600400. The trailing job/flags are part of the
    // virtual ABI but the current body does not read them.
    L2_GUARD_FUNCTION();
    if (count == 0) {
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    CItem* const prototype = object != nullptr && object->IsItem() ? static_cast<CItem*>(object) : nullptr;
    if (prototype == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"Creature_server.cpp",
        //     0x35c7,
        //     itemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[inventory] Cannot remove item class %d from object %u: the prototype is missing or is not an item (requested
        //     count %lld).", itemClassId, m_nId, static_cast<long long>(count))
        L2_LOG_ERROR(
            Log,
            "[inventory] Cannot remove item class %d from object %u: the prototype is missing or is not an item (requested "
            "count %lld).",
            itemClassId, m_nId, static_cast<long long>(count)
        );
        return false;
    }

    std::int64_t remaining = count;
    L2_WRITE_GUARD(m_inv);
    if (!CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
        CSharedItemData* itemData = m_inv.GetFirst();
        while (itemData != nullptr && remaining > 0) {
            CSharedItemData* const next = itemData->Next();
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (item && itemData->m_nClassID == itemClassId && IsNPC()) {
                itemData->m_nCount = 0;
                --remaining;
                m_inv.Pop(itemData->index, false);
            }
            itemData = next;
        }
    } else {
        CSharedItemData* itemData = m_inv.GetFirst();
        while (itemData != nullptr && remaining > 0) {
            CSharedItemData* const next = itemData->Next();
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!item) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]ERROR, Cannot Find item object. "
                //     L"item classid[%d][%s]",
                //     L"Creature_server.cpp",
                //     0x35da,
                //     itemData->m_nClassID,
                //     GetName())
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Skipping item class %d for %s during stack removal: shared index %d has no live object.
                //     continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
                // L2_LOG_ERROR(Log,
                //     L"[inventory] Skipping item class %d for %s during stack removal: shared index %d has no live object.
                //     continuing the scan.", itemData->m_nClassID, GetName(), itemData->index)
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Skipping item class %d for %s during stack removal: shared index %d has no live object; "
                    u"continuing the scan.",
                    itemData->m_nClassID, GetName(), itemData->index
                );
                itemData = next;
                continue;
            }
            if (itemData->m_nClassID == itemClassId) {
                if (IsNPC()) {
                    const std::int64_t realDelete = std::min(itemData->m_nCount, remaining);
                    itemData->m_nCount -= realDelete;
                    remaining -= realDelete;
                    if (itemData->m_nCount == 0) {
                        m_inv.Pop(itemData->index, false);
                    }
                }
                break;
            }
            itemData = next;
        }
    }
    return remaining == 0;
}

int CCreature::GetCorpseTime() const { return 300; }

double CCreature::IntBonusF() const { return g_PCParamDB.GetParamBonus(PC_INT_BONUS, m_data->m_Stat.m_N[0]); }

double CCreature::DexBonusF() const { return g_PCParamDB.GetParamBonus(PC_DEX_BONUS, m_data->m_Stat.m_N[4]); }

double CCreature::MenBonusF() const { return g_PCParamDB.GetParamBonus(PC_MEN_BONUS, m_data->m_Stat.m_N[3]); }

double CCreature::ConBonusF() const { return g_PCParamDB.GetParamBonus(PC_CON_BONUS, m_data->m_Stat.m_N[2]); }

double CCreature::StrBonusF() const { return g_PCParamDB.GetParamBonus(PC_STR_BONUS, m_data->m_Stat.m_N[1]); }

double CCreature::WitBonusF() const { return g_PCParamDB.GetParamBonus(PC_WIT_BONUS, m_data->m_Stat.m_N[5]); }

double CCreature::LucBonusF() const { return g_PCParamDB.GetParamBonus(PC_LUC_BONUS, m_data->m_Stat.m_N[6]); }

double CCreature::ChaBonusF() const { return g_PCParamDB.GetParamBonus(PC_CHA_BONUS, m_data->m_Stat.m_N[7]); }

bool CCreature::ChangeClass(const ClassType classType, int level) {
    // Current L2Server 0x006156d8. `level` is kept by the public ABI but
    // the current body never reads it.
    L2_GUARD_FUNCTION();
    static_cast<void>(level);

    const std::uint32_t rawClass = static_cast<std::uint32_t>(classType);
    if (rawClass >= 0xc0U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"class is invalid for changing: %d",
        //     static_cast<int>(classType))
        // Log.Add(
        //     LOG_ERROR,
        //     "[creature] Class change rejected for object %u: class value %d is outside 0..191.",
        //     m_nId,
        //     static_cast<int>(classType))
        L2_LOG_ERROR(
            Log, "[creature] Class change rejected for object %u: class value %d is outside 0..191.", m_nId,
            static_cast<int>(classType)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }

    {
        L2_GUARD(*m_lock);
        std::int32_t classValue = static_cast<std::int32_t>(m_data->m_nClass);
        m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(classValue, static_cast<std::int32_t>(classType), 3);
        m_data->m_nClass = static_cast<ClassType>(classValue);
        AcquireSkills(g_SkillAcquireDB.m_vectorClsSkill[static_cast<std::size_t>(m_data->m_nClass)], true, false);
    }

    if (IsPC()) {
        CastUser()->OnRuneItemsChange();
    }
    SetMaxMagicLevel();
    m_data->m_NeedUpdatePacketBitSet.SetAllBits();
    return true;
}

int CCreature::GetClass() const { return m_data->m_nClass; }

double CCreature::HPRegen() const { return m_data->m_dHPRegen; }

double CCreature::BaseHP() const { return m_data->m_dBaseHP; }

double CCreature::MPRegen() const { return m_data->m_dMPRegen; }

double CCreature::BaseMP() const { return m_data->m_dBaseMP; }

double CCreature::PAttack() const { return m_data->m_dParam[2]; }

double CCreature::PDefend() const { return m_data->m_dParam[0]; }

double CCreature::PParry() const { return 0.0; }

double CCreature::CarryWeight() const { return m_data->m_dCarryWeight; }

MoveModeEnum CCreature::GetMoveMode() {
    if (m_data->m_MovingStatus == ::MSTE_MOVING) {
        return static_cast<MoveModeEnum>((m_data->m_MoveMode != ::MT_SLOW) + MVMD_WALK);
    }
    return static_cast<MoveModeEnum>((m_data->m_StopMode & ~::STP_STORE) != ::STP_SIT);
}

bool CCreature::CheckShowMovePos(const int emitterId, const FVector& position) {
    // Current L2Server 0x0060e170.
    L2_GUARD_FUNCTION();

    if (!m_bShowMovePosToGM) {
        return false;
    }

    std::vector<FVector> points;
    points.push_back(position);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, observer, {
        observer->ShowTraceEmiter(static_cast<std::int16_t>(emitterId), points, 30000);
    });
    return true;
}

void CCreature::ShowBlocks(const std::vector<FVector>& points) {
    // Current L2Server 0x0060dfa0.
    L2_GUARD_FUNCTION();

    if (!IsPC()) {
        return;
    }

    std::vector<FVector> tracePoints;
    for (const FVector& point : points) {
        tracePoints.push_back(point);
        const std::int32_t ground = CWorld::Inst().GetGeo()->FindGround(
            runtime::Win64CvttDoubleToInt32(point.x), runtime::Win64CvttDoubleToInt32(point.y),
            runtime::Win64CvttDoubleToInt32(point.z)
        );
        tracePoints.push_back(
            FVector{
                point.x,
                point.y,
                static_cast<double>(runtime::AddInt32Wrapping(ground, 40)),
            }
        );
    }
    CastUser()->ShowTraceEmiter(0, tracePoints, 30000);
}

void CCreature::ShowLines(
    User& user,
    std::vector<FVector>& points,
    int duration,
    unsigned char red,
    unsigned char green,
    unsigned char blue,
    int emitterId
) {
    L2_GUARD_FUNCTION();

    CUserSocket* const socket = user.GetSocket();
    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][TW_CRASH] No User Socket.",
        //     kCreatureServerSourceFile,
        //     0x385d)
        // Log.Add(
        //     LOG_WARN,
        //     "[movement] Trace lines not sent to player object %u (emitter %d, %zu points): the player socket is missing.",
        //     user.m_nId,
        //     emitterId,
        //     points.size())
        L2_LOG_WARN(
            Log,
            "[movement] Trace lines not sent to player object %u (emitter %d, %zu points): the player socket is missing.",
            user.m_nId, emitterId, points.size()
        );
        return;
    }

    // Exact function-local current buffer owner at 02f8bb50.
    static char buffer[0x5000]{};
    int packetLength = 0;
    for (const FVector& point : points) {
        packetLength += ::Assemble(
            buffer + packetLength, static_cast<int>(sizeof(buffer)) - packetLength, "ddd",
            runtime::Win64CvttDoubleToInt32(point.x), runtime::Win64CvttDoubleToInt32(point.y),
            runtime::Win64CvttDoubleToInt32(point.z)
        );
    }

    socket->Send(
        "chhdcccbd", 0xfe, 0xa6, static_cast<int>(points.size()), duration, static_cast<int>(red), static_cast<int>(green),
        static_cast<int>(blue), packetLength, buffer, emitterId
    );
}

void CCreature::ShowLoc(User* user) {
    // Current L2Server 0x0060d56c.
    L2_GUARD_FUNCTION();

    const FAngle direction = GetDir();
    const int yaw = direction.Yaw & 0xffff;
    const FVector position = GetPos();

    char16_t location[0x400]{};
    FormatUtf16To(
        location, std::size(location), u"%ls %d, %d, %d, %d", GetName(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), yaw
    );
    if (user != nullptr) {
        user->GetSocket()->SendSystemMessage(u"SYS", location);
    }

    std::vector<FVector> points;
    points.push_back(position);
    const FVector vector{100.0, 0.0, 0.0};
    const int directionForLine = GetDir().Yaw & 0xffff;
    const FVector axis{0.0, 0.0, 1.0};
    const FVector rotated = FVector::RotateAngleAxis(&vector, directionForLine, &axis);
    FVector destination = position;
    destination.x += rotated.x;
    destination.y += rotated.y;
    destination.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z + 40.0)
    ));
    points.push_back(destination);

    const int emitterId = runtime::Int32FromBits(m_nId);
    if (user != nullptr) {
        ShowLines(*user, points, 10, 0xff, 0, 0, emitterId);
        return;
    }

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, observer, { ShowLines(*observer, points, 10, 0xff, 0, 0, emitterId); });
}

bool CCreature::IsFrozen(bool) { return m_data->m_bIsFreezed; }

int CCreature::GetAttackRange() const { return m_data->m_nAttackRange; }

int CCreature::InventoryWeight() const { return 0; }

FVector CCreature::RandomDirection() { return FVector{4.0, 4.0, 4.0}; }

::ArmorType CCreature::GetArmorType() const { return m_data->m_ArmorType; }

std::int64_t CCreature::HasItem(unsigned int) { return 0; }

unsigned int CCreature::GetMyAirShipID() { return m_data->m_AirShipId; }

void CCreature::SetMyAirShipID(unsigned int airShipId) {
    m_data->m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_data->m_AirShipId, airShipId, 65);
}

bool CCreature::TeleportWithAirShip(CAirShip*, FVector, unsigned int) { return false; }

bool CCreature::GetOnAirShip(CAirShip*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CCreature::GetOffAirShip(CAirShip*, FVector) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CCreature::Die(CCreature* killer, int reason) {
    // Current L2Server 0x00622e48.
    L2_GUARD_FUNCTION();

    enum class EarlyDeathAction {
        None,
        Duel,
        Olympiad,
        PledgeGame,
    };
    EarlyDeathAction earlyAction = EarlyDeathAction::None;

    {
        L2_GUARD(*m_lock);

        if (g_bMoveToLeader && !IsPCOrSummon()) {
            AbandonAttackLeaderOf(m_spTarget.GetCreature());
        }
        if (m_data->m_bIsAlive == 0) {
            return false;
        }
        if (m_bIsRestarting) {
            m_data->SetHP(1.0);
            m_data->m_LastAttackerServerID = killer != nullptr ? static_cast<std::int32_t>(killer->Id()) : 0;
            return false;
        }
        if (IsPCOrPCRelatedNPC() && killer != nullptr && killer->Id() == Id()) {
            m_data->SetHP(1.0);
            return false;
        }
        if (IsPC() && killer != nullptr && IsDuelEnemy(killer)) {
            m_data->SetHP(1.0);
            earlyAction = EarlyDeathAction::Duel;
        }

        if (earlyAction == EarlyDeathAction::None && IsPC() && CastUser()->IsOlympiadPlayer()) {
            m_data->SetHP(1.0);
            earlyAction = EarlyDeathAction::Olympiad;
        }
        if (earlyAction == EarlyDeathAction::None && IsPC() && CastUser()->m_PledgeGameUser.IsPlayer()) {
            m_data->SetHP(1.0);
            earlyAction = EarlyDeathAction::PledgeGame;
        }

        if (earlyAction == EarlyDeathAction::None) {
            m_data->m_LastAttackerServerID = killer != nullptr ? static_cast<std::int32_t>(killer->Id()) : 0;

            CCreature* const lastAttacker =
                g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_data->m_LastAttackerServerID));
            if (IsPC() && IsAlive() && lastAttacker != nullptr && TriggerSkillByDeath(*lastAttacker) > 0) {
                m_data->SetHP(1.0);
                return false;
            }

            if (IsAlive() && lastAttacker != nullptr) {
                CCreature* triggerOwner = lastAttacker;
                if (lastAttacker->IsSummonOrPet()) {
                    triggerOwner = lastAttacker->GetUserOrMaster();
                }
                if (triggerOwner != nullptr) {
                    triggerOwner->TriggerSkillByKill(*this);
                }
            }

            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_bIsAlive, 0, 0x61);
            m_data->SetHP(0.0);
            SendStatusUpdate(VCP_HP, 0, 0);
            m_MPRewardList.clear();
        }
    }

    if (earlyAction == EarlyDeathAction::Duel) {
        CDuelManager::inst().DuelPlayerDie(CastUser(), killer);
        return false;
    }
    if (earlyAction == EarlyDeathAction::Olympiad) {
        WOlympiadMatch* const field = CastUser()->m_pWOlympiadUser->GetField();
        if (field != nullptr) {
            field->PlayerDied(CastUser());
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] OlympiadField null user(%s)",
            //     kCreatureServerSourceFile,
            //     0x892,
            //     L"CCreature::Die",
            //     GetName())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[combat] Cannot report Olympiad defeat for %s: the match is missing. Normal death processing remains
            //     bypassed.", GetName())
            // L2_LOG_ERROR(Log,
            //     L"[combat] Cannot report Olympiad defeat for %s: the match is missing. Normal death processing remains
            //     bypassed.", GetName())
            L2_LOG_ERROR(
                Log,
                u"[combat] Cannot report Olympiad defeat for %s: the match is missing; normal death processing remains "
                u"bypassed.",
                GetName()
            );
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] OlympiadUser died-_- (%s)",
        //     kCreatureServerSourceFile,
        //     0x895,
        //     L"CCreature::Die",
        //     GetName())
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[combat] Normal death processing bypassed for %s in Olympiad mode.",
        //     GetName())
        // L2_LOG_INFO(Log,
        //     L"[combat] Normal death processing bypassed for %s in Olympiad mode.",
        //     GetName())
        L2_LOG_INFO(Log, u"[combat] Normal death processing bypassed for %s in Olympiad mode.", GetName());
        return false;
    }
    if (earlyAction == EarlyDeathAction::PledgeGame) {
        CPledgeGameField* const field = CastUser()->m_PledgeGameUser.GetField();
        if (field != nullptr) {
            field->PlayerDied(CastUser(), killer);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] PledgeGameField null user(%s)",
            //     kCreatureServerSourceFile,
            //     0x8a5,
            //     L"CCreature::Die",
            //     GetName())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[combat] Cannot report clan-game defeat for %s: the field is missing. Normal death processing remains
            //     bypassed.", GetName())
            // L2_LOG_ERROR(Log,
            //     L"[combat] Cannot report clan-game defeat for %s: the field is missing. Normal death processing remains
            //     bypassed.", GetName())
            L2_LOG_ERROR(
                Log,
                u"[combat] Cannot report clan-game defeat for %s: the field is missing; normal death processing remains "
                u"bypassed.",
                GetName()
            );
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] PledgeGameField died-_- (%s)",
        //     kCreatureServerSourceFile,
        //     0x8a8,
        //     L"CCreature::Die",
        //     GetName())
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[combat] Normal death processing bypassed for %s in clan-game mode.",
        //     GetName())
        // L2_LOG_INFO(Log,
        //     L"[combat] Normal death processing bypassed for %s in clan-game mode.",
        //     GetName())
        L2_LOG_INFO(Log, u"[combat] Normal death processing bypassed for %s in clan-game mode.", GetName());
        return false;
    }

    if (IsPC()) {
        User* const user = CastUser();
        if (user != nullptr && user->m_DuelUserInfo.IsInIndividualDuel()) {
            CDuelManager::inst().CancelDuel(user, 0);
        }
    }

    if (IsNPC() && killer != nullptr && killer->IsPCOrSummon()) {
        User* killerUser = nullptr;
        if (killer->IsPC()) {
            killerUser = killer->CastUser();
        } else if (CNPC* const killerNpc = killer->CastNPC()) {
            killerUser = killerNpc->GetMaster();
        }
        if (killerUser != nullptr) {
            killerUser->SetLastKillTime(static_cast<std::int32_t>(GetSmartID().Sid()), runtime::GetTickCount());
        }
    }
    if (killer != nullptr && killer->IsPCOrPCRelatedNPC() && IsNPC()) {
        CNPC* const npc = CastNPC();
        if (npc != nullptr && npc->GetNPCId() == 0x0f8d84) {
            g_PledgeGameSystem.IncreaseKillCountByKillingNPC(killer);
        }
    }

    DoNothing();
    if (IsPC()) {
        User* const user = CastUser();
        if (user != nullptr && user->m_pFishingController != nullptr &&
            (user->m_pFishingController->m_FishingState != 0 || user->m_pFishingController->m_bIsAutoFishing)) {
            if (CCreatureController* const controller = user->GetController()) {
                controller->DoAutoFishing(false);
            }
        }
    }

    if (!m_bIsGMKill && reason == 0) {
        m_pContribute->Distribute(this, killer);
    }
    if (IsBoss()) {
        CBoss* const boss = CastBoss();
        if (boss != nullptr && boss->m_bIsFieldBoss) {
            m_pContribute->DistributeSubExpSp(this);
        }
    }

    CCreature* resolvedKiller =
        g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_data->m_LastAttackerServerID));
    if (resolvedKiller != nullptr && resolvedKiller->IsPCTrap()) {
        resolvedKiller = static_cast<CCreature*>(resolvedKiller->CastPCTrap()->GetMaster());
        if (resolvedKiller == nullptr) {
            return false;
        }
    }

    bool damageAllowsPvpPenalty = true;
    if (IsPCOrSummon() || IsSummonedNpc()) {
        switch (static_cast<DamageTypeEnum>(m_eDeadDamage)) {
            case DAMAGE_NORMAL:
            case DAMAGE_SKILL:
            case DAMAGE_OVER_HIT:
            case DAMAGE_CHRONO:
            case DAMAGE_FIST:
                break;
            default:
                damageAllowsPvpPenalty = false;
                break;
        }
    }

    bool inChaoPermissionBattleField = false;
    if (resolvedKiller != nullptr) {
        inChaoPermissionBattleField = InChaoPermissionBattleField(*resolvedKiller);
    }

    bool victimInEventMatch = false;
    if (IsPC()) {
        victimInEventMatch = CastUser()->m_EventMatchUser.m_byEventMatchTeamId != 0;
    } else if (IsSummonOrPet()) {
        victimInEventMatch =
            CastSummon()->GetMaster() != nullptr && CastSummon()->GetMaster()->m_EventMatchUser.m_byEventMatchTeamId != 0;
    } else if (IsSummonedNpc()) {
        victimInEventMatch =
            CastNPC()->GetMaster() != nullptr && CastNPC()->GetMaster()->m_EventMatchUser.m_byEventMatchTeamId != 0;
    }

    if (!m_bIsGMKill && (IsPCOrSummon() || IsSummonedNpc()) && damageAllowsPvpPenalty && resolvedKiller != nullptr &&
        (resolvedKiller->IsPCOrSummon() || resolvedKiller->IsSummonedNpc()) && inChaoPermissionBattleField &&
        !victimInEventMatch && m_data->m_nKarma >= 0) {
        User* killerUser = nullptr;
        if (resolvedKiller->IsPC()) {
            killerUser = resolvedKiller->CastUser();
        } else if (CNPC* const npc = resolvedKiller->CastNPC()) {
            killerUser = npc->GetMaster();
        }

        std::int32_t killerDatabaseId = 0;
        std::int32_t killerWorldId = g_nWorldId;
        if (resolvedKiller->IsSummonOrPet()) {
            killerDatabaseId = static_cast<std::int32_t>(resolvedKiller->CastSummon()->m_MasterUserDBID);
        } else if ((resolvedKiller->IsSummonedNpc() || resolvedKiller->IsSummonedFriendNpc()) && killerUser != nullptr) {
            killerDatabaseId = static_cast<std::int32_t>(killerUser->GetDBID());
        }
        if (resolvedKiller->IsSummonOrPet()) {
            killerWorldId = resolvedKiller->CastSummon()->m_MasterWorldID;
        }

        CCreature* effectiveKiller = resolvedKiller;
        CNPC* effectiveKillerOwner = nullptr;
        if (resolvedKiller->IsSummonOrPet()) {
            effectiveKillerOwner = resolvedKiller->CastSummon();
        } else if (resolvedKiller->IsSummonedNpc() || resolvedKiller->IsSummonedFriendNpc()) {
            effectiveKillerOwner = resolvedKiller->CastNPC();
        }
        if (effectiveKillerOwner != nullptr) {
            if (User* const master = effectiveKillerOwner->GetMaster()) {
                effectiveKiller = master;
            }
        }

        CCreature* effectiveVictim = this;
        CNPC* effectiveVictimOwner = nullptr;
        if (IsSummonOrPet()) {
            effectiveVictimOwner = CastSummon();
        } else if (IsSummonedNpc() || IsSummonedFriendNpc()) {
            effectiveVictimOwner = CastNPC();
        }
        if (effectiveVictimOwner != nullptr) {
            if (User* const master = effectiveVictimOwner->GetMaster()) {
                effectiveVictim = master;
            }
        }

        bool declaredWar = false;
        if (effectiveVictim->IsPC() && effectiveKiller->IsPC()) {
            declaredWar = effectiveKiller->CastUser()->CheckDeclareWarBoth(effectiveVictim->CastUser());
        }

        if (!IsSummonOrPet() && !IsSummonedNpc()) {
            if (declaredWar || !IsMakeAttackerChao(effectiveKiller)) {
                if (killerUser == nullptr) {
                    db.IncreaseUserDuelCount(killerDatabaseId, killerWorldId);
                } else {
                    killerUser->IncreaseDuelCount();
                    killerUser->ParamChanged(std::int64_t{PCB_USER});
                }
            } else {
                if (AreaBasedRule::KarmaPenalty(this) && killerUser != nullptr && !killerUser->m_bEinhasadeTransformed) {
                    killerUser->DecreaseKarma(true, CastUser());
                    killerUser->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_DIE});
                    killerUser->SendETCBuffStatusInfo();
                    if (g_bPKKarmaPenalty) {
                        killerUser->ValidateOrgPDefend();
                        killerUser->ValidateOrgMDefend();
                    }
                }

                CSharedItemData* const weapon = killerUser != nullptr ? killerUser->GetEquipedWeapon() : nullptr;
                if (killerUser != nullptr && killerUser->m_data->m_nKarma > 0 && weapon != nullptr &&
                    ((weapon->m_nClassID >= 0x1e88 && weapon->m_nClassID <= 0x1e97) ||
                        (weapon->m_nClassID >= 0x2180 && weapon->m_nClassID <= 0x218f))) {
                    killerUser->UnequipArmor(ST_RHAND);
                    killerUser->UnequipArmor(ST_LHAND);
                    killerUser->UnequipArmor(ST_RLHAND);
                }
            }
        } else if (!declaredWar && IsMakeAttackerChao(effectiveKiller)) {
            if (killerUser == nullptr) {
                db.DecreaseUserKarma_KillSummon(killerDatabaseId, killerWorldId);
            } else {
                killerUser->DecreaseKarma_KillSummon();
                killerUser->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_KILL});
                killerUser->SendETCBuffStatusInfo();
            }
        }
    } else if (IsPC() && m_data->m_nKarma < 0 && resolvedKiller != nullptr) {
        User* killerUser = nullptr;
        if (resolvedKiller->IsPC()) {
            killerUser = resolvedKiller->CastUser();
        } else if (CNPC* const npc = resolvedKiller->CastNPC()) {
            killerUser = npc->GetMaster();
        }
        if (g_bInterludeKarma) {
            if (!m_bIsGMKill && killerUser != nullptr && killerUser != CastUser()) {
                killerUser->IncreaseDuelCount();
                killerUser->ParamChanged(std::int64_t{PCB_USER});
            }
        } else if (CastUser()->CanGiveKillerKarmaPoint(killerUser)) {
            killerUser->IncreaseKarmaByKillingChao();
            killerUser->ParamChanged(std::int64_t{PCB_USER});
            CastUser()->SetKilledTimeByUserAsChao(runtime::crt::Time32Now());
            CastUser()->SaveGiveKillerKarmaPoint();
        }
    }

    FVector deathPosition = m_data->m_Pos;
    if (!IsPC()) {
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, deathPosition, "cddddddddddcddb", 0, static_cast<int>(Id()), 0, 0, 0, 0, m_nMercenaryKey != 0, 0,
            0, 0, 0, 0, 0, 0, nullptr
        );
    } else {
        User* const user = CastUser();
        user->LogWhenPCDie(resolvedKiller, deathPosition);

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(user, summon, {
            g_npcserver.Send("cdd", 0x37, static_cast<int>(summon->Id()), 2);
            summon->ChangeTarget(nullptr, CTR_DEFAULT);
            summon->DoNothing();
        });

        const char save = resolvedKiller == nullptr || resolvedKiller->IsPCOrSummon() ? 0 : 2;
        user->SendDiePacket(save);
    }

    bool preserveAbnormal = false;
    switch (m_SkillMod.m_nPreserveAbnormal) {
        case 0:
            preserveAbnormal = true;
            break;
        case 1:
            preserveAbnormal = resolvedKiller != nullptr && resolvedKiller->IsPC();
            break;
        case 2:
            preserveAbnormal = resolvedKiller != nullptr && resolvedKiller->IsNPC();
            break;
        default:
            break;
    }

    const auto& resurrectionZones = m_SkillDurationMod.m_vResurrectionInstantZoneIds;
    const bool listedResurrectionZone =
        std::find(resurrectionZones.begin(), resurrectionZones.end(), static_cast<int>(m_data->m_nInstantZoneTypeID)) !=
        resurrectionZones.end();
    const bool preserveByResurrectionSpecial =
        listedResurrectionZone || (m_SkillDurationMod.m_nResurrectionSpecial > 0 && resurrectionZones.empty());

    if (!preserveAbnormal && !preserveByResurrectionSpecial) {
        DispelAllByGM(SPET_DIE, false);
    } else {
        L2_GUARD(m_InfectedSkillsSect);
        _DeleteAbnormalStatus(ABNS_RESURRECTION_SPECIAL, this, nullptr, true, SPET_DIE, false);
        _DeleteAbnormalStatus(ABNS_REDUCE_DROP_PENALTY, this, nullptr, true, SPET_DIE, false);
        _DeleteAbnormalStatus(ABNS_PRESERVE_ABNORMAL, this, nullptr, true, SPET_DIE, false);
        _DeleteAbnormalStatus(ABNS_MYSTERIOUS_DYE, this, nullptr, true, SPET_DIE, false);
        _DeleteAbnormalStatus(ABNS_RICHNESS_TALISMAN, this, nullptr, true, SPET_DIE, false);
    }

    m_bAbnormalStatusChanged = true;
    m_nEnergyCount = 0;
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_UltimateSkillGauge, 0, 0x59);
    if (!preserveAbnormal && !preserveByResurrectionSpecial) {
        m_nSongDanceCount = 0;
    }

    if (!IsPC()) {
        ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        ParamChanged();
    } else {
        CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        ParamChanged(std::int64_t{PCB_CHAR_DIE});
    }
    m_dwFakeDeathStartTime = 0;
    m_dwMPowerStorage = 0;
    m_sPowerCount = -1;
    m_nDeathCount.store(0, std::memory_order_relaxed);
    ResetListTargetMe();
    return true;
}

void CCreature::Suicide(DamageTypeEnum, int) {
    L2_GUARD_FUNCTION();
    Die(nullptr, 0);
}

void CCreature::SendResurrectionConfirmDlg() {}

void CCreature::AttackOutOfRange() {}

void CCreature::OnKillNpc(CCreature*) {}

void CCreature::SocialAction(int, int, bool) {}

unsigned int CCreature::GetMyShuttleID() { return m_data->m_nShuttleId; }

void CCreature::SetMyShuttleID(unsigned int shuttleId) {
    m_data->m_NeedUpdatePacketBitSet.SetUnsignedIntValue_IfModifed(m_data->m_nShuttleId, shuttleId, 65);
}

void CCreature::InitCoupleAction() {
    L2_GUARD_FUNCTION();
    if (IsPC()) {
        CastUser()->InitCoupleAction();
    }
}

bool CCreature::CoupleAction(int actionId, bool, bool, int targetId) {
    L2_GUARD_FUNCTION();

    CObject* const object = CObject::GetObjectW(static_cast<std::uint32_t>(targetId));
    User* target = nullptr;
    if (object != nullptr && object->IsPC()) {
        target = object->CastUser();
    }

    User* const actor = CastUser();
    if (target != nullptr && actor != nullptr) {
        actor->m_nCoupleID = targetId;
        target->m_nCoupleID = static_cast<std::int32_t>(actor->GetServerId());
        actor->m_nCoupleOption = actionId;
        target->m_nCoupleOption = actionId;

        actor->IsCoupleUserRotation();
        target->IsCoupleUserRotation();

        CObject* const moveTarget = target->m_spTarget.GetObjectW();
        target->GetController()->MoveToTarget(moveTarget, false, SkillUseInfo(), 0);
    }
    return true;
}

bool CCreature::IncreaseSoulCount(int, const int) { return false; }

bool CCreature::DecreaseSoulCount(int) { return false; }

bool CCreature::CanSoulCollectByExp(std::int64_t) const { return false; }

bool CCreature::IsCameraMode() { return false; }

bool CCreature::IsInCombatMode() {
    // Current L2Server 0x005e1fb8.
    return m_data->m_bIsInCombatMode;
}

void CCreature::SetInCombatMode(bool inCombat) { m_data->m_bIsInCombatMode = inCombat; }

CCreatureController* CCreature::GetController() { return m_pController; }

void CCreature::CheckRelaxType() {}

void CCreature::OnCorpseDecayed() {}

bool CCreature::CanDoAction_china() { return true; }

void CCreature::AnnouncePickItem(CItem*) {}

bool CCreature::IsSkillDebugger() const { return false; }

bool CCreature::IsSkillDebugMode() const { return false; }

bool CCreature::CanForceAttack() const { return true; }

CParty* CCreature::GetParty() { return nullptr; }

CUserSocket* CCreature::GetSocket() const { return nullptr; }

CCreature* CCreature::GetTargetWithoutFail() {
    L2_GUARD_FUNCTION();
    CCreature* const target = m_spTarget.GetCreature();
    return target ? target : this;
}

int CCreature::GetHPToShow() const {
    // Current L2Server 0x005f07b4.
    return runtime::Win64CvttDoubleToInt32(m_data->m_dHP);
}

void CCreature::AttackTestByCount(CCreature& target, const int count, User& builder) {
    // Current L2Server 0x005f8760.
    L2_GUARD_FUNCTION();
    static std::array<char16_t, 0x2800> buffer{};

    if (count <= 0) {
        return;
    }

    CAttackAction::TimeData timeData;
    CAttackAction::GetTimeData(*this, timeData);

    AttackTargetDataType targetData;
    // The current stack owner leaves this lane indeterminate before the
    // first GenerateAttackTargetData call. Every reached consumer writes it
    // before reading it, so keep that physical initialization boundary.
    bool soulShot;
    double totalDamage = 0.0;
    double criticalDamage = 0.0;
    int hitCount = 0;
    int criticalCount = 0;
    int normalCount = 0;
    int missCount = 0;
    for (int index = 0; index < count; ++index) {
        CAttackAction::GenerateAttackTargetData(this, &target, targetData, soulShot, 1.0, false);
        if (!target.IsPCOrPCRelatedNPC() && IsPCOrPCRelatedNPC()) {
            const int attackerLevel = GetUserOrMaster() != nullptr ? GetUserOrMaster()->Level() : Level();
            targetData.dDamage *= CNewActionHelper::inst().GetHighLevelDamageFactor(target.Level(), attackerLevel);
        }

        totalDamage += targetData.dDamage;
        if (!targetData.bIsHit) {
            ++missCount;
        } else {
            ++hitCount;
            if (!targetData.bIsCritical) {
                ++normalCount;
            } else {
                criticalDamage += targetData.dDamage;
                ++criticalCount;
            }
        }
    }

    const std::uint32_t totalTimeMilliseconds = timeData.GetAllTime() * static_cast<std::uint32_t>(count);
    const double totalTime = static_cast<double>(totalTimeMilliseconds) / 1000.0;
    const double hitTime = static_cast<double>(timeData.m_nHitTime) / 1000.0;
    const double coolTime = static_cast<double>(timeData.m_nCoolTime) / 1000.0;
    const double secondHitTime = static_cast<double>(timeData.m_nSecondHitTime) / 1000.0;

    FormatUtf16To(
        buffer.data(), buffer.size(),
        u"Try Attack [%d]\r\nAttack Test Time Result\r\n"
        u"Time - total : %.2f\r\n"
        u"Each - (Hit/Cool/Second) = (%.2f/%.2f/%.2f)\r\n",
        count, totalTime, hitTime, coolTime, secondHitTime
    );
    builder.SendSystemMessage(u"SYS", buffer.data());

    const double normalDamage = totalDamage - criticalDamage;
    FormatUtf16To(
        buffer.data(), buffer.size(),
        u"Attack Test Damage Result\r\n"
        u"== Damage ==\r\n"
        u"(Total/Crit/Normal)=(%d/%d/%d)\r\n"
        u"== Hit ==\r\n"
        u"(Total/Crit/Normal/Miss)=(%d/%d/%d/%d)\r\n"
        u"== Prob ==\r\n"
        u"(Crit/Miss)=(%.2f/%.2f)\r\n"
        u"== Avg ==\r\n"
        u"(Total/Crit/Normal)=(%.2f/%.2f/%.2f)\r\n"
        u"== DPS=(%.2f) ==\r\n",
        runtime::Win64CvttDoubleToInt32(totalDamage), runtime::Win64CvttDoubleToInt32(criticalDamage),
        runtime::Win64CvttDoubleToInt32(normalDamage), count, criticalCount, normalCount, missCount,
        static_cast<double>(criticalCount) / static_cast<double>(hitCount),
        static_cast<double>(missCount) / static_cast<double>(count), totalDamage / static_cast<double>(hitCount),
        criticalDamage / static_cast<double>(criticalCount), normalDamage / static_cast<double>(normalCount),
        totalDamage / totalTime
    );
    builder.SendSystemMessage(u"SYS", buffer.data());
}

void CCreature::AttackTestBySec(CCreature& target, const int seconds, User& builder) {
    // Current L2Server 0x005f8b3c.
    L2_GUARD_FUNCTION();
    static std::array<char16_t, 0x2800> buffer{};

    CAttackAction::TimeData timeData;
    CAttackAction::GetTimeData(*this, timeData);
    FormatUtf16To(buffer.data(), buffer.size(), u"attack_test in [%d] secs\r\n", seconds);
    builder.SendSystemMessage(u"SYS", buffer.data());

    const std::uint32_t attackCount =
        static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(seconds, 1000)) / timeData.GetAllTime();
    AttackTestByCount(target, runtime::Int32FromBits(attackCount), builder);
}

void CCreature::SkillTest(CCreature& target, CSkillInfo& skillInfo, const int count, User& builder) {
    // Current L2Server 0x005f8c5c.
    L2_GUARD_FUNCTION();
    static std::array<char16_t, 0x2800> buffer{};

    int hitTime = 0;
    int hitCancelTime = 0;
    int coolTime = 0;
    CSkillAction2::CalcSkillTiming(target, skillInfo, hitTime, hitCancelTime, coolTime);
    const int totalTimeMilliseconds = runtime::MultiplyInt32Wrapping(
        runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(coolTime, hitCancelTime), hitTime), count
    );
    FormatUtf16To(
        buffer.data(), buffer.size(),
        u"Skill Use [%d]\r\nSkill Test Time Result\r\n"
        u"Time - total : %.2f sec\r\n"
        u"Each - HitCancel,Hit,Cool(%.2f,%.2f,%.2f)\r\n",
        count, static_cast<double>(totalTimeMilliseconds) / 1000.0, static_cast<double>(hitTime) / 1000.0,
        static_cast<double>(hitCancelTime) / 1000.0, static_cast<double>(coolTime) / 1000.0
    );
    builder.SendSystemMessage(u"SYS", buffer.data());
}

void CCreature::SetNickName(const char16_t* nickname) {
    L2_GUARD_FUNCTION();
    std::memset(m_sNickName, 0, sizeof(m_sNickName));
    if (nickname) {
        for (std::size_t index = 0; index < 24 && nickname[index] != u'\0'; ++index) {
            m_sNickName[index] = nickname[index];
        }
    } else {
        runtime::process::HandleInvalidParameter();
    }
    m_data->m_NeedUpdatePacketBitSet.SetBit(79);
}

double CCreature::GetCriticalDamagePer() { return m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Per; }

double CCreature::GetCriticalDamageDiff() { return m_SkillMod.m_Mods[CCreatureSkillMod::CriticalDamage].m_Diff; }

void CCreature::LeaveWorld(bool) {
    // Current L2Server 0x005e6790.
    L2_GUARD_FUNCTION();

    m_bLeft = true;
    runtime::raw_atomic::DecrementI32(&g_nHeartbeatingCreatureNum);
    runtime::raw_atomic::DecrementI32(&g_nExistingCreatureNum);
    m_bIsFreezing = true;

    const int oldContainerIndex = m_data->container_index;
    if (!IsInVehicle()) {
        if (!IsInShuttle()) {
            // This apparently extra current call is observable through
            // the virtual dispatch surface and is kept literally.
            IsInAirShip();
        }
        return;
    }

    const int vehicleId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    CVehicle* const vehicle = g_VehicleDB.FindVehicle(vehicleId);
    if (vehicle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "vehicle null (was %d) at file[%s], line[%d]",
        //     oldContainerIndex,
        //     "Creature_server.cpp",
        //     0x3c5)
        // Log.Add(
        //     LOG_ERROR,
        //     "[creature] Cannot move departing object %u to the vehicle exit: vehicle %d was not found for previous
        //     container %d, returning after the leave-state updates.", m_nId, vehicleId, oldContainerIndex)
        L2_LOG_ERROR(
            Log,
            "[creature] Cannot move departing object %u to the vehicle exit: vehicle %d was not found for previous "
            "container %d; returning after the leave-state updates.",
            m_nId, vehicleId, oldContainerIndex
        );
        return;
    }

    CWorld::Inst().MoveTo(this, vehicle->m_vKickPos, true);
    L2_RELEASE(vehicle, ObjectRefType::FindObject);
}

double CCreature::GotDamageByEnvironment(double damage, bool critical, DamageTypeEnum damageType, bool reflect) {
    // Current L2Server 0x005e6fbc.
    L2_GUARD_FUNCTION();

    const double appliedDamage = GotDamage(nullptr, damage, 0.0, critical, damageType, reflect);
    if (damageType == DAMAGE_HEIGHT && IsPC() && appliedDamage > 0.0) {
        CLogFormatter formatter{};
        formatter.WriteLog2_1(0x45d, this, nullptr, static_cast<int>(damage), 0, 0, 0, 0, 0, 0);
    }
    return appliedDamage;
}

double CCreature::GotDamage(
    CCreature* attacker, double damage, double cpDamage, bool critical, DamageTypeEnum damageType, bool allowLuckyDeath
) {
    // Current L2Server 0x00624268.
    L2_GUARD_FUNCTION();

    if (!m_SkillMod.CanGetDamageHP(attacker, this) || !IsAlive()) {
        return 0.0;
    }
    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        if (npc != nullptr && npc->m_nClassId == 0x0fcd4b && damageType != DAMAGE_FIST) {
            return 0.0;
        }
    }

    const double oldHP = m_data->m_dHP;
    double newHP = oldHP - damage;
    double overHitDamage = 0.0;
    if (m_data->m_bUndying && newHP <= 0.0) {
        newHP = 1.0;
    }

    if (newHP <= 0.0) {
        const bool preventHP = m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_HP];
        const bool preventMP = m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_MP];
        const bool preventCP = m_SkillMod.m_bPreventDeathParam[CCreatureSkillMod::PDP_CP];
        bool preventionDeleted = false;
        {
            L2_GUARD(m_InfectedSkillsSect);
            preventionDeleted = _DeleteAbnormalStatus(ABNS_PREVENTION_DEATH, this, nullptr, true, SPET_SELF_DISPEL, false);
        }

        if (preventionDeleted) {
            if (preventHP) {
                m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 0x0b);
                newHP = m_data->m_dBaseHP;
            }
            if (preventMP) {
                m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 0x0c);
            }
            if (preventCP) {
                m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCP, m_data->m_dBaseCP, 0x0d);
            }
        } else if (!allowLuckyDeath) {
            newHP = 1.0;
        } else if (!IsPC()) {
            newHP = 0.0;
            overHitDamage = damage - oldHP;
        } else {
            if (CastUser()->EventByLuckyStat(true)) {
                SkillUseInfo skill{};
                skill.m_skillID = 0x46b8;
                skill.m_skillLevel = 1;
                int zero1 = 0;
                int hitTime = 500;
                int zero2 = 0;
                int reuseDelay = 1500;
                int zero3 = 0;
                int zero4 = 0;
                DefaultIOBuffer* const packet =
                    MakePacketForMagicSkillUse(zero1, this, skill, hitTime, zero2, reuseDelay, zero3, zero4, nullptr);
                g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, GetPos(), GetBroadcastDist(0x1000), packet);
                SendSystemMessage(SystemMessageId::LADY_LUCK_PROTECTED_YOU);
                newHP = oldHP;
            } else if (CastUser()->IsOlympiadPlayer()) {
                WOlympiadMatch* const field = CastUser()->m_pWOlympiadUser->GetField();
                if (field != nullptr) {
                    field->PlayerDied(CastUser());
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][%s] OlympiadField null user(%s)",
                    //     L"Creature_server.cpp",
                    //     0xb4c,
                    //     L"CCreature::GotDamage",
                    //     GetName())
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[combat] Cannot report lethal damage for Olympiad player %s: the match is missing. Continuing
                    //     with HP 1.", GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[combat] Cannot report lethal damage for Olympiad player %s: the match is missing. Continuing
                    //     with HP 1.", GetName())
                    L2_LOG_ERROR(
                        Log,
                        u"[combat] Cannot report lethal damage for Olympiad player %s: the match is missing; continuing "
                        u"with HP 1.",
                        GetName()
                    );
                }
                newHP = 1.0;
            } else if (CastUser()->m_PledgeGameUser.IsPlayer() && CastUser()->m_PledgeGameUser.IsAlive()) {
                CPledgeGameField* const field = CastUser()->m_PledgeGameUser.GetField();
                if (field != nullptr) {
                    field->PlayerDied(CastUser(), attacker);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][%s] CPledgeGameField null user(%s)",
                    //     L"Creature_server.cpp",
                    //     0xb5b,
                    //     L"CCreature::GotDamage",
                    //     GetName())
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[combat] Cannot report lethal damage for clan-game player %s: the field is missing. Continuing
                    //     with HP 1.", GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[combat] Cannot report lethal damage for clan-game player %s: the field is missing. Continuing
                    //     with HP 1.", GetName())
                    L2_LOG_ERROR(
                        Log,
                        u"[combat] Cannot report lethal damage for clan-game player %s: the field is missing; continuing "
                        u"with HP 1.",
                        GetName()
                    );
                }
                newHP = 1.0;
            } else {
                newHP = 0.0;
                overHitDamage = damage - oldHP;
            }
        }
    }

    m_data->SetHP(newHP);
    if (IsPC() && newHP < m_data->m_dBaseHP / 3.0) {
        CastUser()->CheckTutorialEvent(TCE_HP_1OVER3);
    }
    if (attacker == nullptr) {
        SendStatusUpdate(VCP_HP, 0, 0);
    } else {
        SendStatusUpdate(VCP_HP, 0, attacker->m_nId);
        BroadcastHP(0, attacker->m_nId);
    }
    m_data->m_NeedUpdatePacketBitSet.ClearBit(0x0b);

    if (IsPC()) {
        if (CastUser()->m_PledgeGameUser.IsPlayer()) {
            if (CPledgeGameField* const field = CastUser()->m_PledgeGameUser.GetField()) {
                field->RegisterMessage(9, 0, 0, false);
            }
        }
    }

    if (newHP > 0.0) {
        m_data->m_LastAttackerServerID = attacker != nullptr ? attacker->m_nId : 0;
    } else if (critical && IsAlive()) {
        if (damageType == DAMAGE_OVER_HIT) {
            m_data->m_dOverHitBonus = std::min(overHitDamage / m_data->m_dBaseHP + 1.0, 1.25);
        }
        if (IsPCOrSummon() || IsSummonedNpc()) {
            m_eDeadDamage = damageType;
        }
        Die(attacker, 0);
    }

    double appliedDamage = oldHP;
    if (appliedDamage < 0.0) {
        appliedDamage = 0.0;
    } else if (appliedDamage >= damage) {
        appliedDamage = damage;
    }
    OnGotDamage(attacker, damage + cpDamage);
    return appliedDamage;
}

double CCreature::AttackedBy(
    CCreature* attacker, double damage, bool critical, int attackId, AttackType attackType, SlotType slotType
) {
    // Current L2Server 0x00615994.
    L2_GUARD_FUNCTION();
    if (m_data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "data null at file[%s], line[%d]",
        //     "Creature_server.cpp",
        //     0xea5)
        // Log.Add(
        //     LOG_ERROR,
        //     "[combat] Attack ignored for object %u: shared creature data is missing. Returning zero applied damage.",
        //     m_nId)
        L2_LOG_ERROR(
            Log, "[combat] Attack ignored for object %u: shared creature data is missing; returning zero applied damage.",
            m_nId
        );
        return 0.0;
    }
    if (attacker == this) {
        // Log.Add(
        //     LOG_ERROR,
        //     "suiciding? :p at file[%s], line[%d]",
        //     "Creature_server.cpp",
        //     0xeab)
        // Log.Add(
        //     LOG_WARN,
        //     "[combat] Self-attack ignored for object %u. Returning zero applied damage.",
        //     m_nId)
        L2_LOG_WARN(Log, "[combat] Self-attack ignored for object %u; returning zero applied damage.", m_nId);
        return 0.0;
    }

    m_data->m_nLastBlowWeaponClassID = attackId;
    User* attackerUser = attacker != nullptr && attacker->IsPC() ? attacker->CastUser() : nullptr;
    User* targetUser = IsPC() ? CastUser() : nullptr;
    if (targetUser != nullptr && targetUser->IsPC() && targetUser->m_nCoupleID != 0) {
        CCreature* const partnerCreature =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetUser->m_nCoupleID));
        User* const partner = partnerCreature != nullptr && partnerCreature->IsPC() ? partnerCreature->CastUser() : nullptr;
        if (partner != nullptr) {
            targetUser->DoNothing();
            partner->DoNothing();
            targetUser->StopMove();
            partner->StopMove();
            targetUser->InitCoupleParam();
            partner->InitCoupleParam();
            targetUser->GetSocket()->SendSystemMessage(SystemMessageId::COUPLE_ACTION_CANCELLED);
            partner->GetSocket()->SendSystemMessage(SystemMessageId::COUPLE_ACTION_CANCELLED);
        }
    }

    if (attackerUser != nullptr && targetUser != nullptr) {
        CCursedWeaponMgr::inst();
        if (attackerUser->m_nCursedWeaponClassId > 0 && attackerUser->m_nId != targetUser->m_nId) {
            targetUser->m_data->SetCP(0.0);
        }
        if (attackerUser->IsHero()) {
            CCursedWeaponMgr::inst();
            if (targetUser->m_nCursedWeaponClassId > 0 && attackerUser->m_nId != targetUser->m_nId) {
                targetUser->m_data->SetCP(0.0);
            }
        }
    }

    m_bAttackedByRange = attackType == AT_POLE || attackType == AT_TWOHANDSWORD || attackType == AT_TWOHANDBLUNT ||
        attackType == AT_TWOHANDSTAFF || (attackType == AT_ANCIENTSWORD && slotType == ST_RLHAND);

    if (attacker != nullptr && m_SkillMod.NeedToAlterNormalDamage(*attacker)) {
        damage += static_cast<double>(m_SkillMod.m_stAlterNormalDamage.m_nAlterRatio) * damage / 100.0;
    }

    DamageTypeEnum damageType = DAMAGE_NORMAL;
    switch (attackId) {
        case 0x106a:
        case 0x140d:
        case 0x16b9:
        case 0x1b92:
        case 0x209e:
        case 0xb4a9:
        case 0xc286:
            damageType = DAMAGE_CHRONO;
            break;
        default:
            if (attackType == AT_FIST) {
                damageType = DAMAGE_FIST;
            }
            break;
    }

    const double appliedDamage = GotDamageBy(attacker, damage, false, damageType, true);
    if (attacker == nullptr) {
        return appliedDamage;
    }

    attacker->TriggerSkillByAttack(*this, damage, critical);
    if (m_SkillMod.CanGetDamageHP(attacker, this)) {
        if (m_SkillMod.m_bIsReverseAttack) {
            double reverseDamage = (m_SkillMod.m_dReverseDamagePer - 1.0) * damage;
            const double resist = attacker->m_SkillDurationMod.m_dDamageShieldResist;
            if (resist >= 0.0) {
                const double firstClamp = std::min(resist, 100.0);
                if (firstClamp > 0.0) {
                    reverseDamage *= (100.0 - std::clamp(resist, 0.0, 100.0)) / 100.0;
                }
            }
            reverseDamage = std::min(reverseDamage, GetPDefend() + GetShieldDefense());
            if (static_cast<int>(reverseDamage) == 0) {
                return reverseDamage;
            }
            attacker->GotDamageBy(this, reverseDamage, false, DAMAGE_SHIELD, true);
        }

        if (attacker->m_SkillMod.m_bIsVampiricState &&
            ::genrand64_real1() * 100.0 <= attacker->m_SkillMod.m_dVampiricProb * 100.0) {
            attacker->IncreaseHP_diff(
                (attacker->m_SkillMod.m_dVampiricBonusPer - 1.0) * appliedDamage * attacker->m_dMeleeMultiTargetBias *
                    (1.0 - m_SkillMod.m_dVampiricDefencePer),
                false, 4, 0, false
            );
        }

        double mpBonus = 0.0;
        for (int index = 0; index < attacker->m_SkillMod.m_nMPVampiricIndex; ++index) {
            const CCreatureSkillMod::MPVampiric& vampiric = attacker->m_SkillMod.m_MPVampiric[index];
            if (vampiric.m_dProb > 0.0 && ::genrand64_real1() * 100.0 < vampiric.m_dProb) {
                mpBonus += vampiric.m_dBonus;
            }
        }
        if (mpBonus > 0.0) {
            attacker->IncreaseMP_diff(mpBonus / 100.0 * appliedDamage * attacker->m_dMeleeMultiTargetBias, 4, 0, false);
        }
    }

    if (attackType == AT_POLE) {
        attacker->m_dMeleeMultiTargetBias *= 0.5;
    }
    return appliedDamage;
}

double CCreature::GotDamageBy(CCreature* attacker, double damage, bool critical, DamageTypeEnum damageType, bool reflect) {
    // Current L2Server 0x00626e3c.
    L2_GUARD_FUNCTION();

    if (attacker == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "suiciding? :p at file[%s], line[%d]",
        //     "Creature_server.cpp",
        //     0xbc7)
        // Log.Add(
        //     LOG_ERROR,
        //     "[combat] Attacker-based damage ignored for object %u: the attacker is null. Returning zero applied damage.",
        //     m_nId)
        L2_LOG_ERROR(
            Log,
            "[combat] Attacker-based damage ignored for object %u: the attacker is null; returning zero applied damage.",
            m_nId
        );
        return 0.0;
    }
    if (!attacker->IsAlive() || IsFrozen(true) || attacker->IsFrozen(true)) {
        return 0.0;
    }

    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        const int classId = npc->m_nClassId;
        const bool chronoOnly = classId == 0x0f761c || classId == 0x0f761d || classId == 0x0f761e || classId == 0x0f742a ||
            classId == 0x0f742b || classId == 0x0f7519 || classId == 0x0f769c || classId == 0x0f769d || classId == 0x0f769e;
        if ((chronoOnly && damageType != DAMAGE_CHRONO) ||
            ((classId == 0x0f772f || classId == 0x0f7730) && damageType != DAMAGE_EVENT)) {
            return 0.0;
        }
    }

    if (IsNPC() && attacker->IsPC()) {
        CNPC* const npc = CastNPC();
        CBoss* const boss = npc != nullptr ? npc->CastBoss() : nullptr;
        const int owningPledgeId = boss != nullptr ? boss->GetPledgeId() : 0;
        User* const attackerUser = attacker->CastUser();
        if (owningPledgeId > 0 && attackerUser != nullptr &&
            owningPledgeId != static_cast<int>(attackerUser->m_data->m_nPledgeID)) {
            return 0.0;
        }
    }

    if (IsNPC()) {
        CNPC* const npc = CastNPC();
        if (npc->m_data->m_bUnique && GetClassId() == 0x0f8d85) {
            if (nCountAttacked == (nCountAttacked / 10) * 10) {
                CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_data->m_nPledgeID));
                if (pledge) {
                    pledge->SendSystemMessageToMembersWithNpcId(SystemMessageId::CLAN_FLAG_UNDER_ATTACK, GetClassId());
                }
            }
            ++nCountAttacked;
        }
    }

    if (IsTrap()) {
        return 0.0;
    }

    CCreature* duelOwner = nullptr;
    if (IsInDuel() && !attacker->IsPCOrPCRelatedNPC()) {
        duelOwner = this;
    } else if (attacker->IsInDuel() && !IsPCOrPCRelatedNPC()) {
        duelOwner = attacker;
    }
    User* const duelUser = duelOwner != nullptr ? duelOwner->GetUserOrMaster() : nullptr;
    if (duelUser != nullptr && duelUser->m_DuelUserInfo.IsInIndividualDuel() && duelUser->m_DuelUserInfo.IsInFightStatus()) {
        CDuelManager::inst().CancelDuel(duelUser, 0);
    }

    if (IsPCOrSummon() && attacker->IsPCOrSummon()) {
        User* targetUser = nullptr;
        if (IsPC()) {
            targetUser = CastUser();
        } else {
            CSummon* const summon = CastSummon();
            targetUser = summon->GetMaster();
        }
        User* sourceUser = nullptr;
        if (attacker->IsPC()) {
            sourceUser = attacker->CastUser();
        } else {
            CSummon* const summon = attacker->CastSummon();
            sourceUser = summon->GetMaster();
        }
        if (targetUser != nullptr && sourceUser != nullptr) {
            const bool targetPk = targetUser->m_bEinhasadeTransformed || targetUser->m_data->m_nKarma < 0;
            const bool sourcePk = sourceUser->m_bEinhasadeTransformed || sourceUser->m_data->m_nKarma < 0;
            if (targetPk != sourcePk) {
                User* const normalUser = targetPk ? sourceUser : targetUser;
                User* const pkUser = targetPk ? targetUser : sourceUser;
                if (normalUser->m_SkillMod.m_bIsPKProtect &&
                    (!targetUser->m_AreaMod.m_bIsInBattleField || !sourceUser->m_AreaMod.m_bIsInBattleField) &&
                    pkUser->m_data->m_nLevel - normalUser->m_data->m_nLevel > 9) {
                    return 0.0;
                }
            }
        }
    }

    if (IsPC() && attacker->IsPC()) {
        User* const attackerUser = attacker->CastUser();
        if (attackerUser->m_data->m_Race == RT_ELF || attackerUser->m_data->m_Race == RT_DARKELF) {
            User* const relationUser = attacker->CastUser();
            User* const targetUser = CastUser();
            if ((targetUser->GetRelationTo(relationUser) & 0xd000) != 0) {
                damage += damage * 0.1;
            }
        }
    }

    if (!IsPCOrPCRelatedNPC() && attacker->IsPCOrPCRelatedNPC() && !IsPledgeBossNPC()) {
        User* const attackerUser = attacker->GetUserOrMaster();
        User* levelUser = nullptr;
        if (attackerUser != nullptr) {
            levelUser = attacker->GetUserOrMaster();
        }
        const int attackerLevel = attackerUser != nullptr ? levelUser->Level() : attacker->Level();
        damage *= CNewActionHelper::inst().GetHighLevelDamageFactor(Level(), attackerLevel);
    }

    if (!m_SkillMod.CanGetDamageHP(attacker, this)) {
        attacker->SendSystemMessage(SystemMessageId::TARGET_INVULNERABLE);
        return 0.0;
    }
    if (attacker->m_data->m_bNoAttackDamage) {
        return 0.0;
    }

    bool abnormalStatusChanged = m_SkillMod.m_SpeedOutOfCombat;
    {
        L2_GUARD(m_InfectedSkillsSect);
        if (!m_SkillMod.m_bCanAct && damage > 0.0) {
            abnormalStatusChanged =
                _DeleteAbnormalStatus(ABNS_SLEEP, attacker, nullptr, true, SPET_DAMAGE, false) || abnormalStatusChanged;
            abnormalStatusChanged =
                _DeleteAbnormalStatus(ABNS_STUN, attacker, nullptr, true, SPET_DAMAGE, false) || abnormalStatusChanged;
            abnormalStatusChanged =
                _DeleteAbnormalStatus(ABNS_FORCE_MEDITATION, attacker, nullptr, true, SPET_DAMAGE, false) ||
                abnormalStatusChanged;
        }
        if (damage > 0.0) {
            abnormalStatusChanged =
                _DeleteAbnormalStatus(ABNS_CHANGEBODY, attacker, nullptr, true, SPET_DAMAGE, true) || abnormalStatusChanged;
            abnormalStatusChanged = _DeleteAbnormalStatus(ABNS_REAL_TARGET, attacker, nullptr, true, SPET_DAMAGE, false) ||
                abnormalStatusChanged;
        }
    }

    const bool targetWasOutOfCombat = !m_data->m_bIsInCombatMode;
    if (targetWasOutOfCombat) {
        SetInCombatMode(true);
    }
    if (m_dwDamagedTick == 0) {
        m_dwDamagedTick = runtime::GetTickCount();
    }
    CIOObject::SetTimeStamp(&m_nCombatModeTimeStamp);
    CheckSummonBattleTimeStamp();
    if (abnormalStatusChanged) {
        if (IsPC()) {
            CastUser()->ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        } else {
            ValidateChanged(VB_WEAPONARMOR | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
        }
    }
    if (m_data->m_dHP <= 0.0) {
        return 0.0;
    }

    CCreature* attackerMaster = nullptr;
    if (attacker->IsNPC()) {
        attackerMaster = attacker->GetUserOrMaster();
        CNPC* const attackerNpc = attacker->CastNPC();
        if (attackerNpc->IsSummonedNpc()) {
            attackerMaster = attacker->CastNPC()->GetMaster();
        }
    }

    const bool attackerWasOutOfCombat = !attacker->m_data->m_bIsInCombatMode;
    if (attackerWasOutOfCombat) {
        attacker->SetInCombatMode(true);
    }
    if (attacker->m_SkillMod.m_SpeedOutOfCombat) {
        if (attacker->IsPC()) {
            attacker->CastUser()->ValidateChanged(VB_SKILLMOD | VB_SPEED);
        } else {
            attacker->ValidateChanged(VB_SKILLMOD | VB_SPEED);
        }
    }
    CIOObject::SetTimeStamp(&attacker->m_nCombatModeTimeStamp);
    attacker->CheckSummonBattleTimeStamp();
    if (attackerWasOutOfCombat) {
        attacker->BroadcastCombatModeStart();
        attacker->CheckSummonBattleMode();
    }

    if (CCreatureController* const controller = GetController()) {
        controller->OnDamagedMessage(attacker->GetSmartID(), runtime::Win64CvttDoubleToInt32(damage));
    }

    SafeAccumulateContribute(attacker, std::min(m_data->m_dHP, damage));
    if (m_data->m_FirstAttackerServerID == 0) {
        m_data->m_FirstAttackerServerID = static_cast<int>(attacker->m_nId);
    }

    damage += static_cast<double>(
        m_SkillMod.m_nInfectedSkillDamageUPDiff -
        runtime::Win64CvttDoubleToInt32(static_cast<double>(m_SkillMod.m_nInfectedSkillDamageUPPer) * damage / -100.0)
    );

    double damageBonus = 0.0;
    if (attacker->IsBoss()) {
        damageBonus = m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer / 100.0 * damage +
            m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountDiff;
    } else if (attacker->IsNPC()) {
        damageBonus =
            m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer / 100.0 * damage + m_SkillMod.m_PVEDamageBonusInfo.m_dAmountDiff;
    } else if (attacker->IsPC()) {
        damageBonus =
            m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer / 100.0 * damage + m_SkillMod.m_PVPDamageBonusInfo.m_dAmountDiff;
    }

    double limitedDamage = CalcPositionDamage(*attacker, *this, (damage + damageBonus) * attacker->dDPSBonusForBuilder);
    limitedDamage = CSkillEffect_p_block_act_bonus::CalcDamage(*attacker, *this, limitedDamage);
    limitedDamage = GetDamageByLimit(limitedDamage);

    CCreature* transferTarget = nullptr;
    double transferDamage = 0.0;
    bool transferAccepted = false;
    double targetDamage = limitedDamage;
    constexpr double absorbShieldEpsilon = 9.999999747378752e-06;
    if (IsPC() && m_SkillMod.m_dAbsorbShield <= absorbShieldEpsilon && IsPCOrSummon() &&
        m_SkillMod.m_nTransferDamageRatio > 0) {
        transferTarget =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_SkillMod.m_TransferDamageTargetId));
        if (transferTarget != nullptr && transferTarget->IsAlive() &&
            (damageType == DAMAGE_NORMAL || damageType == DAMAGE_OVER_HIT || damageType == DAMAGE_FIST ||
                damageType == DAMAGE_SKILL) &&
            GetPos().DistCube(transferTarget->GetPos()) < 1000000.0) {
            transferDamage = static_cast<double>(m_SkillMod.m_nTransferDamageRatio) * limitedDamage / 100.0;
            if (transferTarget->m_data->m_dHP - transferDamage > 0.0) {
                transferAccepted = true;
                targetDamage = limitedDamage - transferDamage;
            }
        }
    }

    if (targetDamage > 0.0) {
        {
            L2_GUARD(m_InfectedSkillsSect);
            std::vector<int> reduceIndices;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                if (HasReduceSkill(runtime::CheckedVectorAt(m_InfectedSkills, index))) {
                    reduceIndices.push_back(static_cast<int>(index));
                }
            }
            for (std::size_t count = reduceIndices.size(); count != 0; --count) {
                InfectedSkillType& infected = runtime::CheckedVectorAt(
                    m_InfectedSkills, static_cast<std::size_t>(runtime::CheckedVectorAt(reduceIndices, count - 1))
                );
                --infected.nReduceCount;
                infected.dReduceDamage -= targetDamage;
            }
        }

        User* const damagedUser = IsPC() && IsAlive() ? CastUser() : nullptr;
        User* const sourceUser =
            attacker->IsPC() ? attacker->CastUser() : (attackerMaster != nullptr ? attackerMaster->CastUser() : nullptr);
        if (damagedUser != nullptr && sourceUser != nullptr) {
            CLogFormatter formatter{};
            formatter.WriteLog1_1(
                0x0460, damagedUser, sourceUser, nullptr, nullptr, nullptr,
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(targetDamage)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(damagedUser->m_data->m_dHP)),
                static_cast<std::int64_t>(damageType), static_cast<std::int64_t>(attacker->m_data->m_SummonType),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(damagedUser->m_data->m_dCP)), 0, 0
            );
        } else if (attacker->IsPC() && IsNPC() && IsBoss() && m_data->m_dHP <= m_data->m_dBaseHP * 0.75 &&
            m_data->m_dHP != m_data->m_dBaseHP * 0.75 && m_dwBossNPCHurtTick + 60000U < runtime::GetTickCount()) {
            CNPC* const npc = CastNPC();
            User* const user = static_cast<User*>(attacker);
            m_dwBossNPCHurtTick = runtime::GetTickCount();
            const FVector position = GetPos();
            const char16_t* const userName = user->GetName();
            const int trailingNpcId2 = npc->GetNPCId();
            const int trailingNpcId1 = npc->GetNPCId();
            const auto accountId = user->GetAccountID();
            const auto userDbId = user->GetDBID();
            const int leadingNpcId2 = npc->GetNPCId();
            const int leadingNpcId1 = npc->GetNPCId();
            // Account names are target UTF-16. This formatter consumes native wide strings.
            // Log.AddT(
            //     LOG_AUDIT,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,,,,,,,,,%I64d,%I64d,%s,%s,",
            //     static_cast<std::int64_t>(0x0580),
            //     static_cast<std::int64_t>(leadingNpcId1),
            //     static_cast<std::int64_t>(leadingNpcId2),
            //     static_cast<std::int64_t>(userDbId),
            //     static_cast<std::int64_t>(accountId),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(
            //             m_data->m_dBaseHP)),
            //         static_cast<std::int64_t>(
            //             runtime::Win64CvttDoubleToInt32(
            //                 limitedDamage)),
            //     static_cast<std::int64_t>(trailingNpcId1),
            //     static_cast<std::int64_t>(trailingNpcId2),
            //     userName,
            //     user->m_sAccountName.data())
            // Log.AddT(
            //     LOG_AUDIT,
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,,,,,,,,,%I64d,%I64d,%s,%s,",
            //     static_cast<std::int64_t>(0x0580),
            //     static_cast<std::int64_t>(leadingNpcId1),
            //     static_cast<std::int64_t>(leadingNpcId2),
            //     static_cast<std::int64_t>(userDbId),
            //     static_cast<std::int64_t>(accountId),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(
            //             m_data->m_dBaseHP)),
            //         static_cast<std::int64_t>(
            //             runtime::Win64CvttDoubleToInt32(
            //                 limitedDamage)),
            //     static_cast<std::int64_t>(trailingNpcId1),
            //     static_cast<std::int64_t>(trailingNpcId2),
            //     userName,
            //     Utf16ToWide(user->m_sAccountName.data()).c_str())
            Log.AddT(
                LOG_AUDIT,
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,,,,,,,,,%I64d,%I64d,%s,%s,",
                static_cast<std::int64_t>(0x0580), static_cast<std::int64_t>(leadingNpcId1),
                static_cast<std::int64_t>(leadingNpcId2), static_cast<std::int64_t>(userDbId),
                static_cast<std::int64_t>(accountId), static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(limitedDamage)),
                static_cast<std::int64_t>(trailingNpcId1), static_cast<std::int64_t>(trailingNpcId2), userName,
                user->m_sAccountName.data()
            );
        }

        bool needsNpcEvent = false;
        if (IsNPC()) {
            CNPC* const npc = CastNPC();
            needsNpcEvent = npc->CanGetNpcEvent();
        }
        if (!needsNpcEvent && attacker->IsPC()) {
            needsNpcEvent = attacker->CastUser()->HaveSummon() || attacker->CastUser()->HaveSummonedNpc();
        }
        if (needsNpcEvent && damageType != DAMAGE_SHIELD) {
            const CCreatureSkillMod::Mod& hateBonus = attacker->m_SkillMod.m_Mods[CCreatureSkillMod::HateBonus];
            const double modifiedDamage = limitedDamage * hateBonus.m_Per + hateBonus.m_Diff;
            if (modifiedDamage - limitedDamage != 0.0) {
                const int shieldDefense = attacker->GetShieldDefense();
                const int attackerIndex = attacker->GetIndex();
                const int targetIndex = GetIndex();
                g_npcserver.Send(
                    "cdddddf", 0x20, m_nId, targetIndex, attacker->m_nId, attackerIndex,
                    runtime::Win64CvttDoubleToInt32(modifiedDamage), shieldDefense
                );
            }
        }
    }

    bool absorbed = false;
    double hpDamage = CalcDamageWithAbsorbShield(targetDamage, absorbed);
    double appliedDamage = 0.0;
    if (!absorbed) {
        double absorbedMPDamage = 0.0;
        hpDamage = CalcDamageWithMPShield(hpDamage, absorbedMPDamage);
        if (absorbedMPDamage != 0.0 && hpDamage == 0.0) {
            TriggerSkillByDamage(*attacker, absorbedMPDamage);
        }
        appliedDamage = GotDamageWithCp(attacker, attackerMaster, hpDamage, critical, damageType, reflect);
        SendMessageOnDamaged(
            this, attacker, runtime::Win64CvttDoubleToInt32(hpDamage), damageType != DAMAGE_TRANSFER, true, 0,
            runtime::Win64CvttDoubleToInt32(-hpDamage)
        );
        if (transferAccepted && transferTarget != nullptr) {
            transferTarget->GotDamageBy(attacker, transferDamage, false, DAMAGE_TRANSFER, false);
        }
    } else {
        attacker->SendSystemMessage(SystemMessageId::TARGET_INVULNERABLE);
        SendMagicAttckInfo(attacker, this, MagicAttackInfo::ImmuneForBadSkill);
    }

    if (targetWasOutOfCombat) {
        BroadcastCombatModeStart();
        CheckSummonBattleMode();
    }
    if (!abnormalStatusChanged && IsAlive()) {
        m_bParamChanged.exchange(0, std::memory_order_seq_cst);
    }
    if (m_data->m_nClass == CT_BOSS || static_cast<int>(m_data->m_nClass) == 0x52) {
        User* const user = attacker->GetUserOrMaster();
        if (user != nullptr) {
            g_npcserver.Send("cddQ", 199, user->m_nId, m_nId, appliedDamage);
        }
    }
    return appliedDamage;
}

double CCreature::GotDamageWithCp(
    CCreature* attacker, CCreature* attackerMaster, double damage, bool critical, DamageTypeEnum damageType, bool reflect
) {
    // Current L2Server 0x005e7120.
    L2_GUARD_FUNCTION();

    const double oldCP = m_data->m_dCP;
    double hpDamage = damage;
    if (IsPC() && attacker != nullptr && (attacker->IsPC() || attackerMaster != nullptr) && !critical) {
        hpDamage = 0.0;
        if (oldCP - damage >= 0.0) {
            m_data->SetCP(oldCP - damage);
        } else {
            hpDamage = damage - oldCP;
            m_data->SetCP(0.0);
        }
        SendStatusUpdate(VCP_CP, 0, attacker->m_nId);
        if (IsPC()) {
            CastUser()->BroadcastCP(0, attacker->m_nId);
        }
    }
    return GotDamage(
        attacker, hpDamage, oldCP - m_data->m_dCP,
        // Current L2Server 0x005e72b8 passes literal true here.
        // The incoming critical flag controls the CP branch above. It does
        // not make an ordinary non-critical hit non-lethal.
        true, damageType, reflect
    );
}

double CCreature::CalcDamageWithAbsorbShield(double damage, bool& absorbed) {
    // Current L2Server 0x006249e0.
    L2_GUARD_FUNCTION();

    constexpr double epsilon = 9.999999747378752e-06;
    if (m_SkillMod.m_dAbsorbShield <= epsilon) {
        return damage;
    }

    absorbed = true;
    if (m_SkillMod.m_bLimitDamageInAbsorbShield) {
        damage = 1.0;
    }
    const double remaining = m_SkillMod.m_dAbsorbShield - damage;
    if (remaining <= epsilon) {
        m_SkillMod.m_dAbsorbShield = 0.0;
        m_SkillMod.m_bLimitDamageInAbsorbShield = false;
        {
            L2_GUARD(m_InfectedSkillsSect);
            _DeleteAbnormalStatus(ABNS_ABSORB_SHIELD, nullptr, nullptr, true, SPET_DAMAGE, false);
        }
        if (IsPC()) {
            CastUser()->ValidateChanged(VB_SKILLMOD);
        } else {
            ValidateChanged(VB_SKILLMOD);
        }
    } else {
        m_SkillMod.m_dAbsorbShield = remaining;
    }
    return 0.0;
}

double CCreature::CalcDamageWithMPShield(double damage, double& absorbedMP) {
    // Current L2Server 0x00624b8c.
    L2_GUARD_FUNCTION();

    if (m_SkillMod.m_MPShieldPer < 0) {
        return damage;
    }

    damage -= static_cast<double>(m_SkillMod.m_MPShieldPer) * damage / 100.0;
    const double mpCost = static_cast<double>(m_SkillMod.m_MPShieldCostPer) / 100.0 * damage;
    damage -= mpCost;
    const double remainingMP = m_data->m_dMP - mpCost;
    if (remainingMP < 0.0) {
        m_data->SetMP(0.0);
        damage -= remainingMP;
        {
            L2_GUARD(m_InfectedSkillsSect);
            _DeleteAbnormalStatus(ABNS_MP_SHIELD, nullptr, nullptr, true, SPET_DAMAGE, false);
        }
        if (IsPC()) {
            CastUser()->ValidateChanged(VB_SKILLMOD);
        } else {
            ValidateChanged(VB_SKILLMOD);
        }
        if (IsPC()) {
            CastUser()->SendUserMPInfo(4, 0, false);
            CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::MANA_ARMOR_ENDED_MP_DEPLETED);
        }
    } else {
        IncreaseMP_diff(-mpCost, 4, 0, false);
        if (IsPC()) {
            CastUser()->GetSocket()->SendSystemMessage_d(
                SystemMessageId::MANA_ARMOR_DAMAGE_APPLIED_TO_MP, static_cast<int>(mpCost)
            );
        }
        absorbedMP = mpCost;
    }
    return damage;
}

bool CCreature::InChaoPermissionBattleField(CCreature& creature) {
    // Current L2Server 0x005f0388.
    L2_GUARD_FUNCTION();

    User* self = GetUserOrMaster();
    User* other = creature.GetUserOrMaster();
    if (IsSummonedNpc()) {
        CNPC* const npc = CastNPC();
        self = npc != nullptr ? npc->GetMaster() : nullptr;
    }
    if (creature.IsSummonedNpc()) {
        CNPC* const npc = creature.CastNPC();
        other = npc != nullptr ? npc->GetMaster() : nullptr;
    }
    if (self == nullptr || other == nullptr) {
        return false;
    }

    if (!creature.m_AreaMod.m_bIsInBattleField) {
        return true;
    }
    if (m_AreaMod.m_bIsInBattleField) {
        return true;
    }

    const FVector position = creature.GetPos();
    if (!CWorld::Inst().GetGeo()->IsField(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
            GEO_FIELD_BATTLE_RESIDENCE
        )) {
        return true;
    }

    const std::uint32_t castleIndex = static_cast<std::uint32_t>(self->m_nCurCastleId - 1);
    const auto currentCastleStatus = [castleIndex](User* user) {
        // The current executable accepts index 9 and so reads the
        // next physical dword, m_nFortressUnderOffenceSiegeId. Express the
        // same target-memory contract without invoking host C++ array OOB.
        if (castleIndex < 9U) {
            return user->m_nCastleSiegeStatus[castleIndex];
        }
        if (castleIndex == 9U) {
            return user->m_nFortressUnderOffenceSiegeId;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] castle_index is wrong!! [%d]",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\user.h",
        //     0xd3d,
        //     castleIndex)
        // Log.Add(
        //     LOG_ERROR,
        //     "[combat] Castle-status lookup rejected for player object %u: index %u is outside the supported 0..9 range.
        //     using status 0.", user->m_nId, castleIndex)
        L2_LOG_ERROR(
            Log,
            "[combat] Castle-status lookup rejected for player object %u: index %u is outside the supported 0..9 range; "
            "using status 0.",
            user->m_nId, castleIndex
        );
        runtime::process::ReportCallStack(2, false);
        return 0;
    };
    return currentCastleStatus(self) != 0 && currentCastleStatus(other) != 0;
}

std::int64_t CCreature::SetExp(std::int64_t experience) {
    // Current L2Server 0x005e7c98.
    if (IsPC()) {
        if (m_data->m_nSubJobId > 0 && !m_data->m_IsDualClass && experience >= ExpByLevel[80]) {
            experience = ExpByLevel[80] - 1;
        } else if (experience >= ExpByLevel[120]) {
            experience = ExpByLevel[120] - 1;
        }

        if (g_nCountryCode == 1 && CastUser()->GetSocket()->m_bEconomicRestriction && experience >= ExpByLevel[40]) {
            experience = ExpByLevel[40] - 1;
        }
    } else if (experience >= ExpByLevel[121]) {
        experience = ExpByLevel[121] - 1;
    }

    if (experience < 0) {
        experience = 0;
    }
    if (m_data->m_nExp != experience) {
        m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nExp, experience, 6);
        m_data->m_NeedUpdatePacketBitSet.SetBit(7);
    }

    if (IsPC()) {
        CastUser()->ValidateChanged(VB_LEVEL);
    } else {
        ValidateChanged(VB_LEVEL);
    }
    return m_data->m_nExp;
}

std::int64_t CCreature::SetSP(std::int64_t sp) {
    // Current L2Server 0x005e7dec. This is non-virtual.
    L2_GUARD_FUNCTION();
    if (sp < 0) {
        sp = 0;
    }
    m_data->SetSP(sp);
    return m_data->m_nSP;
}

std::int64_t CCreature::SPInc(std::int64_t sp) {
    // Current L2Server 0x005e7e34. Win64 signed addition wraps in the
    // original machine code, so do the arithmetic through its bit pattern.
    L2_GUARD_FUNCTION();

    constexpr std::int64_t kMaximumSp = 50000000000LL;
    const std::int64_t oldSp = m_data->m_nSP;
    const std::int64_t requestedSp = sp;
    const std::int64_t accumulated = runtime::AddInt64Wrapping(oldSp, sp);
    if (accumulated < kMaximumSp + 1) {
        m_data->SetSP(accumulated);
    } else {
        m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nSP, kMaximumSp, 14);
        sp = kMaximumSp - oldSp;
    }

    if (IsPC()) {
        User* const user = CastUser();
        if (user != nullptr) {
            user->m_UserStatData.AddUserStat2(UserStat::US_SP, requestedSp);
        }
    }
    return sp;
}

bool CCreature::IsDuelEnemy(CCreature* creature) {
    // Current L2Server 0x005eed2c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || creature->m_nId == m_nId) {
        return false;
    }

    if (IsPC()) {
        User* const first = CastUser();
        if (first == nullptr || !first->IsInDuel()) {
            return false;
        }
        if (first->IsMySummonOrPet(creature)) {
            return creature->CastSummon()->m_betrayOwnMaster;
        }
        User* const second = creature->GetUserOrMaster();
        return second != nullptr && second->IsInDuel() && first->m_DuelUserInfo.IsEnemy(second);
    }

    if (creature->IsPC()) {
        User* const first = creature->CastUser();
        if (first == nullptr || !first->IsInDuel()) {
            return false;
        }
        if (first->IsMySummonOrPet(this)) {
            return CastSummon()->m_betrayOwnMaster;
        }
        User* const second = GetUserOrMaster();
        return second != nullptr && second->IsInDuel() && first->m_DuelUserInfo.IsEnemy(second);
    }

    User* const first = GetUserOrMaster();
    User* const second = creature->GetUserOrMaster();
    return first != nullptr && second != nullptr && first->IsInDuel() && second->IsInDuel() &&
        first->m_DuelUserInfo.IsEnemy(second);
}

void CCreature::SetInZoneID(unsigned int inZoneId) {
    // Current L2Server 0x005ef5ac.
    L2_GUARD_FUNCTION();

    m_nInstantZoneID = inZoneId;
    m_data->m_nInstantZoneID = inZoneId;
    m_nNextInstantZoneID = 0;

    int typeId = 0;
    if (inZoneId != 0) {
        CObject* const candidate = CGeneralObject::GetObjectW(inZoneId);
        if (candidate != nullptr && candidate->IsInZone()) {
            CInstantZone* const zone = candidate->CastInZone();
            if (zone != nullptr) {
                typeId = zone->m_nTypeId;
            }
        }
    }
    SetInstantZoneTypeID(typeId);
}

void CCreature::PumpStartAllPumpEffects() {
    // Current L2Server 0x006220d4.
    L2_GUARD_FUNCTION();
    if (!m_data->m_bIsActive && IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] why not activate? [%s]",
        //     L"Creature_server.cpp",
        //     0x51cd,
        //     GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[skills] Player %s is inactive while starting stored effects. Continuing.",
        //     GetName())
        // L2_LOG_WARN(Log,
        //     L"[skills] Player %s is inactive while starting stored effects. Continuing.",
        //     GetName())
        L2_LOG_WARN(Log, u"[skills] Player %s is inactive while starting stored effects; continuing.", GetName());
    }

    std::vector<InfectedSkillType> infectedSkills;
    {
        L2_GUARD(m_InfectedSkillsSect);
        infectedSkills = m_InfectedSkills;
    }
    for (const InfectedSkillType& infected : infectedSkills) {
        if (infected.pSkillInfo != nullptr) {
            infected.pSkillInfo->PumpStart(*this, this, SPST_ENTER_WORLD);
        }
    }
}

void CCreature::OnActivate() {
    // Current L2Server 0x00622760.
    L2_GUARD_FUNCTION();
    PumpStartAllPumpEffects();
}

void CCreature::_OnAbnormalStatusChanged(
    const CSkillInfo& skillInfo, CCreature* caster, SkillPumpStartType startType, bool needSendVisualEffect
) {
    // Current L2Server 0x00613900.
    L2_GUARD_FUNCTION();
    PushSerializedMessage(new CSerializedOnInfectedSkillByMessage(caster, this, skillInfo, startType, needSendVisualEffect));
    m_bAbnormalStatusChanged = true;
}

void CCreature::_OnAbnormalStatusDeleted(
    const CSkillInfo& skillInfo, CCreature* caster, SkillPumpEndType endType, int sourceCreatureId
) {
    // Current L2Server 0x00615864.
    L2_GUARD_FUNCTION();
    PushSerializedMessage(new CSerializedPumpEndMessage(caster, this, skillInfo, endType));

    if (endType == SPET_TIMEOUT || endType == SPET_SELF_DISPEL) {
        CCreature* actor = caster;
        if (sourceCreatureId != 0) {
            CCreature* const resolved = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sourceCreatureId));
            if (resolved != nullptr) {
                actor = resolved;
            }
        }
        skillInfo.PostActivateSkillEffects(actor, caster, CSkillInfo::EAT_END);
    }

    _SynergyAuraByAbnormalSlot();
    m_bAbnormalStatusChanged = true;
}

void CCreature::UseETCItem(
    CItem* item, std::int64_t remainingCount, int forceAttack, bool activateImmediately, int summonId
) {
    // Current L2Server 0x006068a0.
    L2_GUARD_FUNCTION();

    CWorldObject* target = m_spTarget.GetWorldObject();
    const ItemDefaultActionEnum action = item->m_const->m_enumDefaultAction;
    static_cast<void>(GetSocket());

    // Current uses the actor's controller action when an item does not have
    // an immediate effect. Immediate item skills use the same magic-skill
    // packet envelope as ordinary skill activation, but never expose an
    // invisible GM through a direct-send fallback.
    const auto broadcastImmediateSkill = [this](
                                             CCreature& packetCaster, bool publishSheathingChange, CWorldObject& skillTarget,
                                             const CSkillInfo& skillInfo, const SkillUseInfo& useInfo,
                                             CWorldObject& broadcastCenter, int pendingWriteLimit, int distance
                                         ) {
        if (publishSheathingChange) {
            packetCaster.m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(
                packetCaster.m_IsSheathingWeapon, false, 0x5a
            );
        } else {
            packetCaster.m_IsSheathingWeapon = false;
        }

        const int parameter1 = 0;
        const int parameter2 = 0;
        const int parameter3 = 0;
        const int parameter4 = 0;
        const int parameter5 = 0;
        const int parameter6 = 0;
        if (!IsInAirShip()) {
            DefaultIOBuffer* const packet = MakePacketForMagicSkillUse(
                parameter1, &skillTarget, useInfo, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
            );
            g_ptlUserMap->BroadcastToNeighborDist(
                &broadcastCenter, pendingWriteLimit, broadcastCenter.GetPos(), distance, packet
            );
            packet->Release();
            return;
        }

        const FVector relativePosition = GetRelPos();
        const std::uint32_t level =
            (static_cast<std::uint32_t>(skillInfo.m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skillInfo.m_nLevel);
        g_ptlUserMap->BroadcastToNeighborDist(
            &broadcastCenter, 0x5000, broadcastCenter.GetPos(), distance, "chddddddddddddhh", 0xfe, 0x74, 0,
            runtime::Int32FromBits(packetCaster.m_nId), runtime::Int32FromBits(skillTarget.m_nId),
            skillInfo.m_nSkillId, runtime::Int32FromBits(level), 0, 0, 0,
            runtime::Win64CvttDoubleToInt32(relativePosition.x), runtime::Win64CvttDoubleToInt32(relativePosition.y),
            runtime::Win64CvttDoubleToInt32(relativePosition.z), static_cast<int>(GetMyAirShipID()), 0, 0
        );
    };

    for (const CSkillInfo* itemSkill : item->m_ItemSkillList) {
        const CSkillInfo* skillInfo = itemSkill;
        if (action > ITEM_ACTION_EQUIP && (action < ITEM_ACTION_SKILL_MAINTAIN || action == ITEM_ACTION_SUMMON_SOULSHOT)) {
            if (const CSkillInfo* const shotSkill = g_ObjectDB.GetSoulShotSkill(action, this)) {
                skillInfo = shotSkill;
            }
        }

        SkillUseInfo useInfo;
        useInfo.m_skillID = skillInfo->m_nSkillId;
        useInfo.m_skillLevel = skillInfo->m_nLevel;
        useInfo.m_skillSubLevel = skillInfo->m_nSubLevel;
        if (skillInfo->m_TargetType == STGT_SELF || skillInfo->m_TargetType == STGT_GROUND) {
            target = this;
        }

        if (!skillInfo->IsValidTarget(this, target, forceAttack != 0)) {
            if (IsPC()) {
                SendSystemMessage(SystemMessageId::INVALID_TARGET);
            }
            continue;
        }

        if (!item->m_const->m_bImmediateEffect) {
            SkillAction(
                skillInfo->m_nSkillId, forceAttack != 0, false, activateImmediately ? nullptr : item,
                activateImmediately ? skillInfo : nullptr, 0, 0
            );
            continue;
        }

        if (skillInfo->CheckOperateCond(this, target, CSkillInfo::OCT_BOTH) != -2) {
            continue;
        }

        CWorldObject* const activationTarget = target->CastWorldObject();
        const bool gmInvisible = IsGMInvisible();
        const int broadcastDistance = gmInvisible ? 0 : GetBroadcastDist(0x1000);

        bool activatedSummonList = false;
        if ((action == ITEM_ACTION_SUMMON_SOULSHOT || action == ITEM_ACTION_SUMMON_SPIRITSHOT) && IsPC()) {
            const bool haveSummon = CastUser()->HaveSummon();
            const bool havePet = haveSummon ? false : CastUser()->HavePet();
            if (haveSummon || havePet) {
                CCreature* const selected = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(summonId));
                CCreature* const petProbe = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(summonId));
                bool selectedIsOwnedAndAlive = false;
                if (selected != nullptr && selected->IsSummon()) {
                    CSummon* const summon = selected->CastSummon();
                    selectedIsOwnedAndAlive = summon->GetMaster() == this && selected->IsAlive();
                } else if (petProbe != nullptr && petProbe->IsPet()) {
                    CPet* const pet = petProbe->CastPet();
                    selectedIsOwnedAndAlive = pet->GetMaster() == this && petProbe->IsAlive();
                }

                if (selectedIsOwnedAndAlive) {
                    if (!gmInvisible) {
                        broadcastImmediateSkill(
                            *selected, true, *selected, *skillInfo, useInfo, *selected, 0xf000, broadcastDistance
                        );
                    }
                    skillInfo->ActivateSkill(this, selected, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                    g_npcserver.Send(
                        "cddddddd", 0x0e, runtime::Int32FromBits(m_nId), GetIndex(),
                        runtime::Int32FromBits(selected->m_nId), skillInfo->m_nSkillId, skillInfo->m_nLevel,
                        skillInfo->m_nSubLevel, skillInfo->m_nNpcNotice
                    );
                } else {
                    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(CastUser(), summon, {
                        if (!gmInvisible) {
                            broadcastImmediateSkill(
                                *summon, false, *summon, *skillInfo, useInfo, *summon, 0xf000, broadcastDistance
                            );
                        }
                        skillInfo->ActivateSkill(this, summon, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
                        g_npcserver.Send(
                            "cddddddd", 0x0e, runtime::Int32FromBits(m_nId), GetIndex(),
                            runtime::Int32FromBits(summon->m_nId), skillInfo->m_nSkillId, skillInfo->m_nLevel,
                            skillInfo->m_nSubLevel, skillInfo->m_nNpcNotice
                        );
                    });
                }
                activatedSummonList = true;
            }
        }

        if (!activatedSummonList) {
            if (!gmInvisible) {
                broadcastImmediateSkill(
                    *this, true, *activationTarget, *skillInfo, useInfo, *this, 0x5000, broadcastDistance
                );
            }
            skillInfo->ActivateSkill(this, activationTarget, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
            g_npcserver.Send(
                "cddddddd", 0x0e, runtime::Int32FromBits(m_nId), GetIndex(),
                runtime::Int32FromBits(activationTarget->m_nId), skillInfo->m_nSkillId, skillInfo->m_nLevel,
                skillInfo->m_nSubLevel, skillInfo->m_nNpcNotice
            );
        }
    }

    if (item->m_const->m_bImmediateEffect && item->m_const->m_enumDefaultAction != ITEM_ACTION_CALL_SKILL) {
        bool deleted = true;
        {
            L2_WRITE_GUARD(m_inv);
            if (remainingCount == 0) {
                if (item->m_data->m_ItemConsumeType == ICT_CHARGE) {
                    deleted = false;
                } else {
                    m_inv.Pop(item->m_data->index, false);
                    m_inv.SetInventoryChanged(item, IUT_DELETE);
                    item->Delete();
                }
            } else {
                {
                    L2_GUARD(*item->m_lock);
                    item->m_data->m_nCount = remainingCount;
                }
                m_inv.SetInventoryChanged(item, IUT_UPDATE);
            }
        }

        if (IsPC() &&
            (item->m_const->m_enumDefaultAction == ITEM_ACTION_SKILL_REDUCE ||
                item->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) &&
            deleted) {
            const int usedItemClassId = item->GetClassId();
            CastUser()->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x12), usedItemClassId, 1);
        }
    }
}

bool CCreature::UseItem(CItem& item, const int count, const bool force, const int unknown) {
    // Current L2Server 0x00615fd4.
    L2_GUARD_FUNCTION();

    const ItemDefaultActionEnum action = item.m_const->m_enumDefaultAction;
    const int itemClassId = item.m_data->m_nClassID;
    const int itemDatabaseId = item.m_data->m_nDBID;

    if (action != ITEM_ACTION_EQUIP && item.m_const->m_nReuseDelay != 0) {
        if (IsPC() && item.m_const->m_nDelayShareGroup > 0) {
            int reuseEnd = 0;
            if (!CastUser()->UseDelayShareItem(item.m_const->m_nDelayShareGroup, item.m_const->m_nReuseDelay, &reuseEnd)) {
                if (reuseEnd > 0) {
                    CastUser()->SendRemainTime(reuseEnd - runtime::crt::Time32Now(), EST_ITEMID, itemClassId, 0, 0, 0);
                }
                return false;
            }
            CastUser()->m_ShortCut.SendUseSharedGroupItem(&item);
        } else {
            std::uint32_t reuseEndTick = 0;
            if (GetItemReuseDelay(itemDatabaseId, &reuseEndTick) && CIOObject::IsInShortTime(reuseEndTick, 0)) {
                if (IsPC()) {
                    const int remainingSeconds = static_cast<int>((reuseEndTick - runtime::GetTickCount()) / 1000U);
                    CastUser()->SendRemainTime(remainingSeconds, EST_ITEMID, itemClassId, 0, 0, 0);
                }
                return false;
            }
        }
    }

    const auto finish = [this, &item, action]() -> bool {
        if (item.m_const->m_nReuseDelay != 0 && (!IsPC() || item.m_const->m_nDelayShareGroup < 1)) {
            SetItemReuseDelay(static_cast<int>(item.GetDBID()), static_cast<std::uint32_t>(item.m_const->m_nReuseDelay));
        }
        if ((action < ITEM_ACTION_SOULSHOT || action > ITEM_ACTION_BLESS_SPIRITSHOT) && IsInvisibleBySkill()) {
            SetVisibleBySkill(true, true);
        }
        return true;
    };

    const auto sendItemSystemMessage = [this, &item](const int messageId) {
        User* const user = CastUser();
        CUserSocket* const socket = user->GetSocket();
        socket->Send("chccd", 0x62, messageId, 1, 3, item.m_data->m_nClassID);
    };

    const auto prepareItemManipulation = [this](User* const user) -> bool {
        const L2ServerLong state = user->GetItemManipulation();
        if (state <= 0 || state == 2) {
            return true;
        }
        if (state != 1) {
            return false;
        }
        user->TradeCancel(false);
        SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
        user->LeaveItemManipulation();
        return true;
    };

    const auto sameShotGrade = [this, &item]() -> bool {
        CSharedItemData* const weapon = GetEquipedWeapon();
        if (weapon == nullptr) {
            return false;
        }
        const int weaponCrystal = static_cast<int>(weapon->GetConstant()->GetCrystalType());
        const int itemCrystal = static_cast<int>(item.m_const->GetCrystalType());
        return BaseCrystal[weaponCrystal] == BaseCrystal[itemCrystal];
    };

    switch (action) {
        case ITEM_ACTION_NONE:
            break;

        case ITEM_ACTION_SKILL_REDUCE:
        case ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS: {
            if (IsPC()) {
                User* const user = CastUser();
                for (const CSkillInfo* const skill : item.m_ItemSkillList) {
                    if (skill->m_nSkillId == 0x85a) {
                        const int zoneId = GetInZoneID();
                        const FVector position = GetPos();
                        if (CTreeLoc::CanUseTree(position, zoneId)) {
                            continue;
                        }
                        CUserSocket* const socket = user->GetSocket();
                        socket->Send("chccd", 0x62, SystemMessageId::CANNOT_SUMMON_DUPLICATE_NEARBY_OBJECT, 1, 3, 0x15b9);
                        // Noise: A nearby tree is an expected item-use restriction already explained by the preceding client
                        // message, routine rejected attempts do not need an operator error. Log.Add(
                        //     LOG_ERROR,
                        //     L"can not summon xmastree. near xmas tree "
                        //     L"exists... char_id[%d]",
                        //     user->GetDBID())
                        return false;
                    }
                }
                if (m_AreaMod.m_bIsInBattleField &&
                    (itemClassId == 0x18f5 || itemClassId == 0x18f6 || itemClassId == 0x2990 || itemClassId == 0x2991)) {
                    sendItemSystemMessage(SystemMessageId::CANNOT_SUMMON_NAMED_ENTITY_ON_BATTLEFIELD);
                    return false;
                }
                if (item.m_const->m_bImmediateEffect) {
                    db.RequestUseItem(&item, static_cast<User*>(this), 1, unknown);
                } else {
                    UseETCItem(&item, 0, count, force, unknown);
                }
                break;
            }

            if (IsPet()) {
                if (!item.m_const->m_bImmediateEffect) {
                    User* const master = CastPet()->GetMaster();
                    if (master != nullptr) {
                        master->SendSystemMessage(SystemMessageId::PET_CANNOT_USE_ITEM);
                    }
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CCreature::UseItem failed. pet use "
                    //     L"only immediate effect item item_type[%d]",
                    //     kCreatureServerSourceFile,
                    //     0x1631,
                    //     itemClassId)
                    // Log.Add(
                    //     LOG_WARN,
                    //     "[items] Skipping item class %d (action %d) for pet object %u: this action requires an
                    //     immediate-effect item.", itemClassId, static_cast<int>(action), m_nId)
                    L2_LOG_WARN(
                        Log,
                        "[items] Skipping item class %d (action %d) for pet object %u: this action requires an "
                        "immediate-effect item.",
                        itemClassId, static_cast<int>(action), m_nId
                    );
                } else if (CastPet()->Usable(&item)) {
                    db.PetUseItem(CastPet(), &item);
                } else {
                    User* const master = CastPet()->GetMaster();
                    if (master != nullptr) {
                        master->SendSystemMessage(SystemMessageId::PET_CANNOT_USE_ITEM);
                    }
                }
                break;
            }

            std::int64_t remaining;
            {
                L2_GUARD(*item.m_lock);
                remaining = runtime::SubtractInt64Wrapping(item.m_data->m_nCount, 1);
            }
            UseETCItem(&item, remaining, 0, false, unknown);
            break;
        }

        case ITEM_ACTION_EQUIP: {
            SlotType equippedSlot = ST_NONE;
            {
                L2_READ_GUARD(m_inv);
                for (int slot = 0; slot < ST_MAX; ++slot) {
                    if (m_data->m_nSlotIndex[slot] == item.m_data->index) {
                        equippedSlot = static_cast<SlotType>(slot);
                        break;
                    }
                }
            }

            if (equippedSlot == ST_NONE) {
                return EquipItem(&item, ST_MAX) != 0;
            }
            if (IsPC()) {
                User* const user = CastUser();
                if (user->m_nCursedWeaponClassId > 0 && CCursedWeaponMgr::inst().FindConst(itemClassId) != nullptr) {
                    return false;
                }
            }
            if (IsEquippedWithForcedItem(equippedSlot)) {
                return false;
            }
            if ((equippedSlot == ST_CHEST || equippedSlot == ST_LEGS || equippedSlot == ST_ONEPIECE ||
                    equippedSlot == ST_ALLDRESS) &&
                m_SkillMod.m_nBlockEquipChestArmorCount > 0) {
                return true;
            }
            UnequipItem(equippedSlot, false);
            return true;
        }

        case ITEM_ACTION_SOULSHOT: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            if (!item.m_const->m_bImmediateEffect) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]ITEM_ACTION_SOULSHOT with none "
                //     L"immediateEffect item. classid[%d]",
                //     "Creature_server.cpp",
                //     0x1659,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Soulshot action skipped for object %u, item class %d: the item has no immediate effect.",
                //     m_nId,
                //     itemClassId)
                L2_LOG_ERROR(
                    Log, "[items] Soulshot action skipped for object %u, item class %d: the item has no immediate effect.",
                    m_nId, itemClassId
                );
                break;
            }
            if (m_data->m_nSoulShot != 0 || user->m_bWaitSoulShotUse) {
                break;
            }
            const int required = EquipedWeaponSoulShotCount();
            if (required < 1) {
                SendSystemMessage(SystemMessageId::CANNOT_USE_SOULSHOTS);
                break;
            }
            if (item.m_data->m_nCount < required) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_SOULSHOTS);
                return false;
            }
            if (!sameShotGrade()) {
                SendSystemMessage(SystemMessageId::SOULSHOT_GRADE_DOES_NOT_MATCH_WEAPON);
                break;
            }
            if (!prepareItemManipulation(user)) {
                return false;
            }
            user->m_bWaitSoulShotUse = true;
            db.RequestUseItem(&item, static_cast<User*>(this), required, unknown);
            break;
        }

        case ITEM_ACTION_SPIRITSHOT:
        case ITEM_ACTION_BLESS_SPIRITSHOT: {
            if (!IsPC()) {
                break;
            }
            static_cast<void>(CastUser());
            if (!item.m_const->m_bImmediateEffect) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]ITEM_ACTION_SOULSHOT with none "
                //     L"immediateEffect item. classid[%d]",
                //     "Creature_server.cpp",
                //     0x169f,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Spiritshot action %d skipped for object %u, item class %d: the item has no immediate
                //     effect.", static_cast<int>(action), m_nId, itemClassId)
                L2_LOG_ERROR(
                    Log,
                    "[items] Spiritshot action %d skipped for object %u, item class %d: the item has no immediate effect.",
                    static_cast<int>(action), m_nId, itemClassId
                );
                break;
            }
            if (m_data->m_bSpiritShot || CastUser()->m_bWaitSpiritShotUse) {
                break;
            }
            const int required = EquipedWeaponSpiritShotCount();
            if (required < 1) {
                SendSystemMessage(SystemMessageId::CANNOT_USE_SPIRITSHOTS);
                break;
            }
            if (item.m_data->m_nCount < required) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_SPIRITSHOTS);
                return false;
            }
            if (!sameShotGrade()) {
                SendSystemMessage(SystemMessageId::SPIRITSHOT_GRADE_DOES_NOT_MATCH_WEAPON);
                break;
            }
            if (IsPC()) {
                const L2ServerLong state = CastUser()->GetItemManipulation();
                if (state > 0 && state != 2) {
                    if (state != 1) {
                        return false;
                    }
                    CastUser()->TradeCancel(false);
                    SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                    CastUser()->LeaveItemManipulation();
                }
            }
            CastUser()->m_bWaitSpiritShotUse = true;
            db.RequestUseItem(&item, static_cast<User*>(this), required, unknown);
            break;
        }

        case ITEM_ACTION_SKILL_MAINTAIN:
            if (IsPC()) {
                if (item.m_ItemSkillList.empty()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item has skill maintain action default, "
                    //     L"but it has no skill info. user[%d], class[%d]",
                    //     "Creature_server.cpp",
                    //     0x17cb,
                    //     CastUser()->GetDBID(),
                    //     itemClassId)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[items] Maintain-skill action skipped for character database ID %u, item class %d: the item skill
                    //     list is empty.", CastUser()->GetDBID(), itemClassId)
                    L2_LOG_ERROR(
                        Log,
                        "[items] Maintain-skill action skipped for character database ID %u, item class %d: the item skill "
                        "list is empty.",
                        CastUser()->GetDBID(), itemClassId
                    );
                } else {
                    UseETCItem(&item, 1, 0, false, unknown);
                }
            }
            break;

        case ITEM_ACTION_MAP_OPEN:
            if (IsPC()) {
                User* const user = CastUser();
                CUserSocket* const socket = user->GetSocket();
                socket->Send("cdc", 0xa3, itemClassId, static_cast<int>(SSQSystem::inst().mSharedInfo->mCurrentStatus));
            } else {
                // Log.Add(LOG_ERROR, L"NPC try to open map!!! WOW lol")
                // Log.Add(
                //     LOG_WARN,
                //     "[items] Map item class %d ignored for non-player object %u.",
                //     itemClassId,
                //     m_nId)
                L2_LOG_WARN(Log, "[items] Map item class %d ignored for non-player object %u.", itemClassId, m_nId);
            }
            break;

        case ITEM_ACTION_ROLL_DICE: {
            if (IsPC() && g_bCanRollDice) {
                if (CastUser()->CanUseDice()) {
                    CastUser()->SetUseDice();
                    const int rolled = runtime::Win64CvttDoubleToInt32(genrand64_real1() * 100.0) % 6 + 1;
                    FVector position = GetPos();
                    const FAngle direction = GetDir();
                    const double distance =
                        static_cast<double>(runtime::Win64CvttDoubleToInt32(genrand64_real2() * 16.0 + 30.0));
                    const double angle =
                        static_cast<double>(static_cast<std::uint16_t>(direction.Yaw)) * 3.14159265358979323846 / 32767.0;
                    position.x += static_cast<double>(runtime::Win64CvttDoubleToInt32(std::cos(angle) * distance));
                    position.y += static_cast<double>(runtime::Win64CvttDoubleToInt32(std::sin(angle) * distance));
                    position.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z)
                    ));
                    const FVector heightProbePosition = GetPos();
                    if (runtime::AbsoluteInt32Wrapping(
                            runtime::SubtractInt32Wrapping(
                                runtime::Win64CvttDoubleToInt32(position.z),
                                runtime::Win64CvttDoubleToInt32(heightProbePosition.z)
                            )
                        ) > 100) {
                        position = GetPos();
                    }

                    CParty* const party = CastUser()->GetParty();
                    const int broadcastDistance = GetBroadcastDist(0x1000);
                    if (party == nullptr) {
                        User* const nameUser = CastUser();
                        User* const socketUser = CastUser();
                        CUserSocket* const socket = socketUser->GetSocket();
                        socket->Send(
                            "chccScd", 0x62, SystemMessageId::CHARACTER_DICE_ROLL_RESULT, 2, 0x0c,
                            nameUser->GetName2(nullptr), 1, rolled
                        );
                    } else {
                        party->BroadcastToAllParty(
                            "chccScd", 0x62, 0x342, 2, 0x0c, CastUser()->GetName2(nullptr), 1, rolled
                        );
                    }
                    const FVector broadcastPosition = GetPos();
                    g_ptlUserMap->BroadcastToNeighborDist(
                        this, 0x5000, broadcastPosition, broadcastDistance, "cdddddd", 0xda,
                        runtime::Int32FromBits(m_nId), itemClassId, rolled,
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z)
                    );

                    User* const positionZUser = CastUser();
                    User* const positionYUser = CastUser();
                    User* const positionXUser = CastUser();
                    User* const accountIdUser = CastUser();
                    User* const databaseIdUser = CastUser();
                    User* const accountNameUser = CastUser();
                    const int logZ = runtime::Win64CvttDoubleToInt32(positionZUser->GetPos().z);
                    const int logY = runtime::Win64CvttDoubleToInt32(positionYUser->GetPos().y);
                    const int logX = runtime::Win64CvttDoubleToInt32(positionXUser->GetPos().x);
                    const char16_t* const actorName = GetName();
                    User* const levelUser = CastUser();
                    User* const classUser = CastUser();
                    User* const sexUser = CastUser();
                    User* const raceUser = CastUser();
                    const int accountId = accountIdUser->GetAccountID();
                    const int databaseId = databaseIdUser->GetDBID();
                    const std::wstring accountName =
                        Utf16ToWide(std::u16string_view(accountNameUser->m_sAccountName.data()));
                    // Log.AddT(
                    //     LOG_BOTH,
                    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                    //     static_cast<std::int64_t>(0x3c7),
                    //     static_cast<std::int64_t>(databaseId),
                    //     static_cast<std::int64_t>(accountId),
                    //     static_cast<std::int64_t>(logX),
                    //     static_cast<std::int64_t>(logY),
                    //     static_cast<std::int64_t>(logZ),
                    //     static_cast<std::int64_t>(raceUser->m_data->m_Race),
                    //     static_cast<std::int64_t>(sexUser->m_data->m_Sex),
                    //     static_cast<std::int64_t>(classUser->m_data->m_nClass),
                    //     static_cast<std::int64_t>(levelUser->m_data->m_nLevel),
                    //     static_cast<std::int64_t>(rolled),
                    //     actorName,
                    //     accountName.c_str())
                    Log.AddT(
                        LOG_BOTH,
                        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                        u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,",
                        static_cast<std::int64_t>(0x3c7), static_cast<std::int64_t>(databaseId),
                        static_cast<std::int64_t>(accountId), static_cast<std::int64_t>(logX),
                        static_cast<std::int64_t>(logY), static_cast<std::int64_t>(logZ),
                        static_cast<std::int64_t>(raceUser->m_data->m_Race),
                        static_cast<std::int64_t>(sexUser->m_data->m_Sex),
                        static_cast<std::int64_t>(classUser->m_data->m_nClass),
                        static_cast<std::int64_t>(levelUser->m_data->m_nLevel), static_cast<std::int64_t>(rolled), actorName,
                        accountName.c_str()
                    );
                } else {
                    SendSystemMessage(SystemMessageId::CANNOT_ROLL_DICE_YET);
                }
            }
            if (IsPC() && !g_bCanRollDice) {
                SendSystemMessage(SystemMessageId::DICE_USE_AND_PURCHASE_DISABLED);
            }
            break;
        }

        case ITEM_ACTION_RECIPE: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            const int recipeId = item.m_const->m_nRecipeId;
            if (recipeId < 1) {
                SendSystemMessage(SystemMessageId::CREATE_ITEM_SKILL_LEVEL_TOO_LOW);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] error, non recipe item try action "
                //     L"recipe. item class id[%d]",
                //     "Creature_server.cpp",
                //     0x1892,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Recipe registration skipped for object %u, item class %d: recipe ID %d must be positive.",
                //     m_nId,
                //     itemClassId,
                //     recipeId)
                L2_LOG_ERROR(
                    Log, "[items] Recipe registration skipped for object %u, item class %d: recipe ID %d must be positive.",
                    m_nId, itemClassId, recipeId
                );
                break;
            }
            CRecipeInfo* const recipe = g_RecipeDB.find(recipeId);
            if (recipe == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] can't find recipe info. item class "
                //     L"id[%d], recipe id[%d]",
                //     "Creature_server.cpp",
                //     0x188b,
                //     itemClassId,
                //     recipeId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Recipe registration skipped for object %u, item class %d: recipe %d has no definition.",
                //     m_nId,
                //     itemClassId,
                //     recipeId)
                L2_LOG_ERROR(
                    Log, "[items] Recipe registration skipped for object %u, item class %d: recipe %d has no definition.",
                    m_nId, itemClassId, recipeId
                );
                break;
            }
            const bool common = recipe->m_nCommonRecipe != 0;
            const int level = common ? user->m_nCommonCreateItemLevel : user->m_nDwarfCreateItemLevel;
            const int maximum = common ? user->m_nMaxCommonRecipeCount : user->m_nMaxDwarvenRecipeCount;
            if (level < 1) {
                user->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_RECIPE_WITHOUT_CREATE_ITEM_SKILL);
            } else if (level < recipe->m_nLevel) {
                user->SendSystemMessage(SystemMessageId::CREATE_ITEM_SKILL_LEVEL_TOO_LOW);
            } else if (user->HaveRecipe(recipeId)) {
                user->SendSystemMessage(SystemMessageId::RECIPE_ALREADY_REGISTERED);
            } else if (user->GetRecipeCount(recipe->m_nCommonRecipe) >= maximum) {
                user->SendSystemMessage(SystemMessageId::RECIPE_REGISTRATION_LIMIT_REACHED);
            } else {
                db.UseRecipeItem(*user, runtime::Int32FromBits(item.GetDBID()));
            }
            break;
        }

        case ITEM_ACTION_CAPSULE:
            if (!IsPC()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CCreature::UseItem failed! Only User uses it!",
                //     kCreatureServerSourceFile,
                //     0x15ea)
                // Log.Add(
                //     LOG_WARN,
                //     "[items] Capsule item class %d ignored for non-player object %u.",
                //     itemClassId,
                //     m_nId)
                L2_LOG_WARN(Log, "[items] Capsule item class %d ignored for non-player object %u.", itemClassId, m_nId);
            } else if (item.m_const->m_bImmediateEffect) {
                CWorldObject* target = m_spTarget.GetWorldObject();
                if (target == nullptr) {
                    target = this;
                }
                item.ActivateMultiSkillList(this, target);
            }
            break;

        case ITEM_ACTION_CALC:
            if (IsPC()) {
                User* const user = CastUser();
                CUserSocket* const socket = user->GetSocket();
                socket->Send("cd", 0xe2, itemClassId);
            }
            break;

        case ITEM_ACTION_SEED: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            if (user == nullptr) {
                break;
            }
            CUserSocket* const socket = user->GetSocket();
            if (socket->IsFullFatigue_china()) {
                user->SendSystemMessage(SystemMessageId::CANNOT_ACT_DUE_TO_FATIGUE);
                break;
            }
            if (!user->m_spTarget.IsValid()) {
                break;
            }
            CCreature* const target = user->m_spTarget.GetCreature();
            if (target == nullptr) {
                break;
            }
            if (target->m_data->m_bUnsowing) {
                user->SendSystemMessage(SystemMessageId::TARGET_CANNOT_BE_SEEDED);
            } else if (target->m_data->m_nDomainID != item.m_const->m_nManorId) {
                user->SendSystemMessage(
                    target->m_data->m_nDomainID < 1 ? SystemMessageId::INCORRECT_TARGET
                                                    : SystemMessageId::SEED_NOT_USABLE_IN_THIS_MANOR
                );
            } else if (target->m_data->m_bIsSow) {
                user->SendSystemMessage(SystemMessageId::TARGET_ALREADY_SEEDED);
            } else {
                UseETCItem(&item, 0, 0, false, unknown);
            }
            break;
        }

        case ITEM_ACTION_HARVEST:
            if (IsPC()) {
                UseETCItem(&item, 0, 0, false, unknown);
            }
            break;

        case ITEM_ACTION_XMASSEAL_OPEN:
            if (IsPC()) {
                User* const user = CastUser();
                CUserSocket* const socket = user->GetSocket();
                socket->Send("cd", 0xf8, itemClassId);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"NPC try to open xmas seal !!! WOW lol")
                // Log.Add(
                //     LOG_WARN,
                //     "[items] Christmas-seal item class %d ignored for non-player object %u.",
                //     itemClassId,
                //     m_nId)
                L2_LOG_WARN(
                    Log, "[items] Christmas-seal item class %d ignored for non-player object %u.", itemClassId, m_nId
                );
            }
            break;

        case ITEM_ACTION_SHOW_HTML: {
            if (!IsPC()) {
                break;
            }
            const char16_t* const targetNameData = g_ItemHtml[static_cast<std::uint32_t>(item.m_nClassId)];
            if (targetNameData == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] html file name is null",
                //     "Creature_server.cpp",
                //     0x17e6)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] HTML filename mapping is null for object %u, item class %d (lookup key %u). Filename
                //     conversion is still reached.", m_nId, itemClassId, static_cast<std::uint32_t>(item.m_nClassId))
                L2_LOG_ERROR(
                    Log,
                    "[items] HTML filename mapping is null for object %u, item class %d (lookup key %u); filename "
                    "conversion is still reached.",
                    m_nId, itemClassId, static_cast<std::uint32_t>(item.m_nClassId)
                );
            }
            const std::u16string_view targetName(targetNameData);
            const std::u16string fileName(targetName);
            std::u16string html;
            User* const user = CastUser();
            if (g_HTMLCache.GetHTMLFile(targetNameData, html, m_data->m_CountryCode)) {
                const std::u16string body(runtime::StringView(html));
                user->ShowHTML(
                    fileName.c_str(), body.c_str(), static_cast<unsigned int>(item.m_nClassId), HTML::LAUNCH, true
                );
            }
            break;
        }

        case ITEM_ACTION_SHOW_TUTORIAL:
            if (IsPC()) {
                const char16_t* const targetNameData = g_ItemHtml[static_cast<std::uint32_t>(item.m_nClassId)];
                if (targetNameData == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] html file name is null",
                    //     "Creature_server.cpp",
                    //     0x17fc)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[items] Tutorial filename mapping is null for object %u, item class %d (lookup key %u). Filename
                    //     conversion is still reached.", m_nId, itemClassId, static_cast<std::uint32_t>(item.m_nClassId))
                    L2_LOG_ERROR(
                        Log,
                        "[items] Tutorial filename mapping is null for object %u, item class %d (lookup key %u); filename "
                        "conversion is still reached.",
                        m_nId, itemClassId, static_cast<std::uint32_t>(item.m_nClassId)
                    );
                }
                const std::u16string_view targetName(targetNameData);
                std::wstring fileName = L"..\\L2text\\";
                fileName += Utf16ToWide(targetName);
                User* const user = CastUser();
                CUserSocket* const socket = user->GetSocket();
                socket->Send("cdS", 0xa6, 2, target_utf16::FromRecoveredWindowsWide(fileName.c_str()).c_str());
            }
            break;

        case ITEM_ACTION_SHOW_SSQ_STATUS:
            if (IsPC()) {
                SSQSystem::inst().SendStatusInfoTo(CastUser());
            }
            break;

        case ITEM_ACTION_SUMMON_SOULSHOT: {
            if (!IsPC()) {
                break;
            }
            if (!CastUser()->HaveSummon() && !CastUser()->HavePet()) {
                SendSystemMessage(SystemMessageId::NO_PET_OR_SERVITOR);
                return false;
            }
            if (!item.m_const->m_bImmediateEffect) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]ITEM_ACTION_SOULSHOT with none "
                //     L"immediateEffect item. classid[%d]",
                //     "Creature_server.cpp",
                //     0x16ea,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Summon soulshot item class %d has no immediate effect for object %u. Continuing the remaining
                //     shot-use checks.", itemClassId, m_nId)
                L2_LOG_ERROR(
                    Log,
                    "[items] Summon soulshot item class %d has no immediate effect for object %u; continuing the remaining "
                    "shot-use checks.",
                    itemClassId, m_nId
                );
            }
            if (CastUser()->m_bWaitSummonSoulShotUse) {
                break;
            }

            int required = 0;
            bool alreadyCharged = false;
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(CastUser(), summon, {
                if (!summon->IsAlive()) {
                    continue;
                }
                if (summon->m_data->m_nSoulShot == 0) {
                    required = runtime::AddInt32Wrapping(required, summon->GetSoulShotCount());
                } else {
                    alreadyCharged = true;
                }
            });
            if (required < 1) {
                if (!alreadyCharged) {
                    SendSystemMessage(SystemMessageId::CANNOT_USE_SHOTS_FOR_DEAD_PET_OR_SERVITOR);
                    return false;
                }
                break;
            }
            if (item.m_data->m_nCount < required) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_PET_OR_SERVITOR_SOULSHOTS);
                return false;
            }
            const L2ServerLong state = CastUser()->GetItemManipulation();
            if (state > 0 && state != 2) {
                if (state != 1) {
                    return false;
                }
                CastUser()->TradeCancel(false);
                SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                CastUser()->LeaveItemManipulation();
            }
            CastUser()->m_bWaitSummonSoulShotUse = true;
            db.RequestUseItem(&item, static_cast<User*>(this), required, unknown);
            break;
        }

        case ITEM_ACTION_SUMMON_SPIRITSHOT: {
            if (!IsPC()) {
                break;
            }
            if (!CastUser()->HaveSummon() && !CastUser()->HavePet()) {
                SendSystemMessage(SystemMessageId::NO_PET_OR_SERVITOR);
                return false;
            }
            if (!item.m_const->m_bImmediateEffect) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]ITEM_ACTION_SPIRITSHOT with none "
                //     L"immediateEffect item. classid[%d]",
                //     "Creature_server.cpp",
                //     0x1736,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Summon spiritshot item class %d has no immediate effect for object %u. Continuing the
                //     remaining shot-use checks.", itemClassId, m_nId)
                L2_LOG_ERROR(
                    Log,
                    "[items] Summon spiritshot item class %d has no immediate effect for object %u; continuing the "
                    "remaining shot-use checks.",
                    itemClassId, m_nId
                );
            }
            if (CastUser()->m_bWaitSummonSpiritShotUse) {
                break;
            }

            int required = 0;
            bool alreadyCharged = false;
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(CastUser(), summon, {
                if (!summon->IsAlive()) {
                    continue;
                }
                if (!summon->m_data->m_bSpiritShot) {
                    required = runtime::AddInt32Wrapping(required, summon->GetSpiritShotCount());
                } else {
                    alreadyCharged = true;
                }
            });
            if (required < 1) {
                if (!alreadyCharged) {
                    SendSystemMessage(SystemMessageId::CANNOT_USE_SHOTS_FOR_DEAD_PET_OR_SERVITOR);
                    return false;
                }
                break;
            }
            if (item.m_data->m_nCount < required) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_PET_OR_SERVITOR_SPIRITSHOTS);
                return false;
            }
            const L2ServerLong state = CastUser()->GetItemManipulation();
            if (state > 0 && state != 2) {
                if (state != 1) {
                    return false;
                }
                CastUser()->TradeCancel(false);
                SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                CastUser()->LeaveItemManipulation();
            }
            CastUser()->m_bWaitSummonSpiritShotUse = true;
            db.RequestUseItem(&item, static_cast<User*>(this), required, 0);
            break;
        }

        case ITEM_ACTION_FISHINGSHOT: {
            if (!IsPC()) {
                break;
            }
            if (!item.m_const->m_bImmediateEffect) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]ITEM_ACTION_FISHINGSHOT with none "
                //     L"immediateEffect item. classid[%d]",
                //     "Creature_server.cpp",
                //     0x177c,
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Fishing-shot action skipped for object %u, item class %d: the item has no immediate effect.",
                //     m_nId,
                //     itemClassId)
                L2_LOG_ERROR(
                    Log,
                    "[items] Fishing-shot action skipped for object %u, item class %d: the item has no immediate effect.",
                    m_nId, itemClassId
                );
                break;
            }
            if (m_data->m_nFishingShot != 0 || CastUser()->m_bWaitFishingShotUse) {
                break;
            }
            if (item.m_data->m_nCount < 1) {
                sendItemSystemMessage(SystemMessageId::NOT_ENOUGH_ITEM_TO_USE);
                return false;
            }

            CSharedItemData* const weapon = GetEquipedWeapon();
            if (weapon == nullptr || weapon->GetConstant()->m_AttackType != AT_FISHINGROD) {
                sendItemSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED);
                break;
            }
            CSharedItemConstant* const weaponConstant = weapon->GetConstant();
            const int weaponCrystal = static_cast<int>(weaponConstant->GetCrystalType());
            const int itemCrystal = static_cast<int>(item.m_const->GetCrystalType());
            if (BaseCrystal[weaponCrystal] != BaseCrystal[itemCrystal]) {
                SendSystemMessage(SystemMessageId::FISHING_SHOT_GRADE_DOES_NOT_MATCH_ROD);
                break;
            }
            if (IsPC()) {
                const L2ServerLong state = CastUser()->GetItemManipulation();
                if (state > 0 && state != 2) {
                    if (state != 1) {
                        return false;
                    }
                    CastUser()->TradeCancel(false);
                    SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                    CastUser()->LeaveItemManipulation();
                }
            }
            CastUser()->m_bWaitFishingShotUse = true;
            db.RequestUseItem(&item, static_cast<User*>(this), 1, unknown);
            break;
        }

        case ITEM_ACTION_KEEP_EXP:
            if (IsPC()) {
                if (CastUser()->IsCharmOfCourageAvailable(item.m_const->GetCrystalType()) &&
                    item.m_const->m_bImmediateEffect) {
                    db.RequestUseItem(&item, CastUser(), 1, 0);
                } else {
                    sendItemSystemMessage(SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET);
                }
            }
            break;

        case ITEM_ACTION_CALL_SKILL:
            if (IsPC()) {
                UseETCItem(&item, 0, 0, true, unknown);
            } else if (IsPet()) {
                User* const master = CastPet()->GetMaster();
                if (master != nullptr) {
                    master->SendSystemMessage(SystemMessageId::PET_CANNOT_USE_ITEM);
                }
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CCreature::UseItem failed. pet use only "
                //     L"immediate effect item",
                //     kCreatureServerSourceFile,
                //     0x164a)
                // Log.Add(
                //     LOG_WARN,
                //     "[items] Call-skill item class %d ignored for pet object %u: this action is supported only for
                //     players.", itemClassId, m_nId)
                L2_LOG_WARN(
                    Log,
                    "[items] Call-skill item class %d ignored for pet object %u: this action is supported only for players.",
                    itemClassId, m_nId
                );
            }
            break;

        case ITEM_ACTION_CREATE_MPCC: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            if (user == nullptr || !user->m_spTarget.IsValid()) {
                break;
            }
            CCreature* const targetCreature = user->m_spTarget.GetCreature();
            if (targetCreature == nullptr || !targetCreature->IsPC()) {
                break;
            }
            User* const target = targetCreature->CastUser();
            if (user->CheckAndSetPendingUseETCItem(&item, PUIT_MPCC_CREATE) && user->AskJoinMPCC(target, MCT_ITEM)) {
                break;
            }
            user->SetPendingUseETCItem(nullptr, PUIT_MPCC_CREATE);
            break;
        }

        case ITEM_ACTION_PEEL: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            if (user == nullptr) {
                return false;
            }
            CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(itemClassId);
            if (itemInfo == nullptr) {
                return false;
            }

            const int maximumUnpackedInventoryCount = itemInfo->GetMaxUnpackableCapsuledItemCount();
            const int unpackedInventoryCount =
                runtime::AddInt32Wrapping(user->m_data->m_nNumInventory, maximumUnpackedInventoryCount);
            const bool normalCapacityOkay = unpackedInventoryCount <= user->GetInventoryLimit();
            const int maximumUnpackedArtifactCount = itemInfo->GetMaxUnpackableCapsuledArtifactCount();
            const int unpackedArtifactCount =
                runtime::AddInt32Wrapping(user->m_data->m_nCurrentArtifactInventoryNum, maximumUnpackedArtifactCount);
            const bool artifactCapacityOkay = unpackedArtifactCount <= user->GetArtifactInventoryLimit();
            if (!normalCapacityOkay || !artifactCapacityOkay) {
                if (!normalCapacityOkay) {
                    user->SendSystemMessage(
                        artifactCapacityOkay ? SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE
                                             : SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE
                    );
                } else {
                    user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
                }
                user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_ITEM_INVENTORY_QUANTITY_LIMIT_EXCEEDED);
                return false;
            }

            std::vector<CapsuledItem*> selectedItems;
            itemInfo->GetUnpackableCapsuledItemList(selectedItems);
            std::array<ItemManip, 100> requests{};
            int requestCount = 0;
            bool containsMenteeConditionItem = false;
            for (CapsuledItem* const selected : selectedItems) {
                CObject* const object = g_ObjectDB.GetObjectW(selected->nItemClassId, false);
                CItem* const createdItem = object->CastItem();
                if (createdItem == nullptr) {
                    continue;
                }
                const std::int64_t randomExtent =
                    runtime::AddInt64Wrapping(runtime::SubtractInt64Wrapping(selected->nMax, selected->nMin), 1);
                const std::int64_t amount = runtime::Win64CvttDoubleToInt64(
                    genrand64_real2() * static_cast<double>(randomExtent) + static_cast<double>(selected->nMin)
                );
                requests[requestCount] =
                    ItemManip(0, selected->nItemClassId, amount, createdItem->m_data->GetEnchanted(), 0, 0, true);
                ++requestCount;
                if (CMentorSetting::inst().m_nMenteeCondItemType == selected->nItemClassId) {
                    containsMenteeConditionItem = true;
                }
            }

            requests[requestCount] = ItemManip(0, itemClassId, -1, item.m_data->GetEnchanted(), 0, 0, true);
            ++requestCount;

            L2_GUARD(user->m_ManipulateQueueLock);
            CPacketHelperEx<20480> packet;
            User::etcMsg message = user->SaveEtcMsg(
                containsMenteeConditionItem, item.m_const->m_bIsPremium, itemClassId,
                runtime::Int32FromBits(item.GetDBID()), item.m_data->m_nCount, std::u16string(), std::u16string(), 0
            );
            User::ManipulateQueue queue = user->SaveManipulateQueue(
                ManipulateType::MANI_ITEM_ACTION_PEEL, 0, requestCount, requests.data(), nullptr, 0, packet,
                item.m_const->m_bIsPremium, ItemGetReason::Etc, message
            );
            const User::ManiItemResultType result = user->_SetManiItemJob(queue);
            if (result == User::MRT_NOJOB) {
                if (user->_HandleItemManipulation(queue)) {
                    if (containsMenteeConditionItem) {
                        g_MenteeWaitingList.AddWaitingList(CastUser(), false);
                    }

                    const bool premium = item.m_const->m_bIsPremium;
                    User* const positionZUser = CastUser();
                    User* const positionYUser = CastUser();
                    User* const positionXUser = CastUser();
                    User* const accountIdUser = CastUser();
                    User* const databaseIdUser = CastUser();
                    User* const accountNameUser = CastUser();
                    const int logZ = runtime::Win64CvttDoubleToInt32(positionZUser->GetPos().z);
                    const int logY = runtime::Win64CvttDoubleToInt32(positionYUser->GetPos().y);
                    const int logX = runtime::Win64CvttDoubleToInt32(positionXUser->GetPos().x);
                    CSharedItemData* const itemData = item.m_data;
                    CSharedCreatureData* const actorData = m_data;
                    const std::uint32_t itemDatabaseId = item.GetDBID();
                    const char16_t* const actorName = GetName();
                    const int accountId = accountIdUser->GetAccountID();
                    const int databaseId = databaseIdUser->GetDBID();
                    const std::wstring accountName =
                        Utf16ToWide(std::u16string_view(accountNameUser->m_sAccountName.data()));
                    // Log.AddT(
                    //     premium ? LOG_LONG_KEEP : LOG_BOTH,
                    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                    //     L"%s,%s,,,%I64d",
                    //     static_cast<std::int64_t>(
                    //         premium ? 0x3e3 : 0x3e6),
                    //     static_cast<std::int64_t>(databaseId),
                    //     static_cast<std::int64_t>(accountId),
                    //     static_cast<std::int64_t>(logX),
                    //     static_cast<std::int64_t>(logY),
                    //     static_cast<std::int64_t>(logZ),
                    //     static_cast<std::int64_t>(actorData->m_Race),
                    //     static_cast<std::int64_t>(actorData->m_Sex),
                    //     static_cast<std::int64_t>(actorData->m_nClass),
                    //     static_cast<std::int64_t>(actorData->m_nLevel),
                    //     static_cast<std::int64_t>(itemData->m_nClassID),
                    //     std::int64_t{-1},
                    //     itemData->m_nCount,
                    //     actorName,
                    //     accountName.c_str(),
                    //     static_cast<std::int64_t>(itemDatabaseId))
                    Log.AddT(
                        premium ? LOG_LONG_KEEP : LOG_BOTH,
                        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                        u"%s,%s,,,%I64d",
                        static_cast<std::int64_t>(premium ? 0x3e3 : 0x3e6), static_cast<std::int64_t>(databaseId),
                        static_cast<std::int64_t>(accountId), static_cast<std::int64_t>(logX),
                        static_cast<std::int64_t>(logY), static_cast<std::int64_t>(logZ),
                        static_cast<std::int64_t>(actorData->m_Race), static_cast<std::int64_t>(actorData->m_Sex),
                        static_cast<std::int64_t>(actorData->m_nClass), static_cast<std::int64_t>(actorData->m_nLevel),
                        static_cast<std::int64_t>(itemData->m_nClassID), std::int64_t{-1}, itemData->m_nCount, actorName,
                        accountName.c_str(), static_cast<std::int64_t>(itemDatabaseId)
                    );
                }
            } else if (result == User::MRT_JOBMAP) {
                user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                return false;
            } else if (result == User::MRT_JOBNOTMAP) {
                // Noise: MRT_JOBNOTMAP means the unpack operation was queued behind another item job. Successful queue
                // insertion is routine and does not prove item delivery. Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"Creature_server.cpp",
                //     0x198a)
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                //     L"Creature_server.cpp",
                //     0x198f)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Unexpected queue result %d while unpacking item class %d, database ID %d for object %u (%d
                //     requested item changes), no immediate handler is invoked in this branch.", static_cast<int>(result),
                //     itemClassId,
                //     itemDatabaseId,
                //     m_nId,
                //     requestCount)
                L2_LOG_ERROR(
                    Log,
                    "[items] Unexpected queue result %d while unpacking item class %d, database ID %d for object %u (%d "
                    "requested item changes); no immediate handler is invoked in this branch.",
                    static_cast<int>(result), itemClassId, itemDatabaseId, m_nId, requestCount
                );
            }
            break;
        }

        case ITEM_ACTION_NICK_COLOR:
            if (IsPC()) {
                User* const user = CastUser();
                CUserSocket* const socket = user->GetSocket();
                socket->Send("chd", 0xfe, 0x84, itemClassId);
            }
            break;

        case ITEM_ACTION_HIDE_NAME: {
            if (!IsPC()) {
                break;
            }
            CPledge* const pledge = CastUser()->GetPledge();
            int dominionId = 0;
            if (pledge == nullptr) {
                const int userDatabaseId = CastUser()->GetDBID();
                dominionId = CDominionManager::Inst()->GetUserDominionSiegeID(userDatabaseId);
            } else {
                const int pledgeId = CastUser()->m_data->m_nPledgeID;
                dominionId = CDominionManager::Inst()->GetPledgeDominionSiegeID(pledgeId);
                if (dominionId == 0) {
                    const int userDatabaseId = CastUser()->GetDBID();
                    dominionId = CDominionManager::Inst()->GetUserDominionSiegeID(userDatabaseId);
                }
            }
            if (dominionId == 0) {
                CastUser()->SendSystemMessage(SystemMessageId::DISGUISE_SCROLL_DOES_NOT_MATCH_TERRITORY);
                break;
            }

            int requiredItemClassId = 0;
            if (CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId)) {
                const std::u16string hideName = WideToUtf16(std::wstring_view(dominion->GetHideNameItemName()));
                requiredItemClassId = g_ObjectDB.GetClassIdFromName(hideName.c_str(), false);
            }
            if (!CDominionManager::Inst()->IsInReadyToDominionSiege()) {
                CastUser()->SendSystemMessage(SystemMessageId::TERRITORY_WAR_DISGUISE_AVAILABILITY_WINDOW);
            } else if (requiredItemClassId == 0 || requiredItemClassId != itemClassId) {
                CastUser()->SendSystemMessage(SystemMessageId::DISGUISE_SCROLL_DOES_NOT_MATCH_TERRITORY);
            } else if (CastUser()->GetPledge() != nullptr && CastUser()->GetPledge()->GetCastle() != nullptr) {
                CastUser()->SendSystemMessage(SystemMessageId::TERRITORY_OWNING_CLAN_MEMBER_CANNOT_DISGUISE);
            } else if (CastUser()->m_data->m_StopMode == STP_STORE || CastUser()->GetPrivateStoreType() != PST_NONE) {
                CastUser()->SendSystemMessage(SystemMessageId::CANNOT_DISGUISE_DURING_PRIVATE_STORE_OR_WORKSHOP);
            } else {
                User* const transformedUser = CastUser();
                if (transformedUser->m_bEinhasadeTransformed || transformedUser->m_data->m_nKarma < 0) {
                    CastUser()->SendSystemMessage(SystemMessageId::CANNOT_DISGUISE_WHILE_CHAOTIC);
                } else {
                    CLogFormatter formatter;
                    formatter.WriteLog2(0x7eb, CastUser(), nullptr, dominionId, 0, 0, 0, 0, 0, 0);
                    CastUser()->RenameByDominionSiege(true, true);
                    db.RequestDestroyItem(&item, 1, CastUser(), 0);
                }
            }
            break;
        }

        case ITEM_ACTION_START_QUEST:
            if (IsPC()) {
                User* const user = CastUser();
                if (user == nullptr) {
                    return false;
                }
                if (user->m_data->m_nWorldId != g_nWorldId) {
                    user->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
                    return false;
                }
                CQuestSystem::inst().StartQuest(*user, item);
            }
            break;

        case ITEM_ACTION_USE_COUNT: {
            if (!IsPC()) {
                break;
            }
            User* const user = CastUser();
            if (user == nullptr) {
                break;
            }
            if (user->m_data->m_nWorldId != g_nWorldId) {
                return false;
            }
            if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
                return false;
            }
            const auto resetWait = [user]() { runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0); };

            CItemConstInfo* const itemInfo = g_ItemConstInfoDB.GetItemConstInfo(itemClassId);
            if (itemInfo == nullptr) {
                resetWait();
                return false;
            }
            const std::int16_t useCount = item.m_data->GetUseCount();
            const std::int16_t maximumUseCount = itemInfo->m_cMaxUseCount;
            std::vector<CreateItemStructInUseCount>& createItems =
                itemInfo->GetCreateItemsInUseCount(static_cast<std::int16_t>(useCount + 1));
            if (createItems.empty()) {
                resetWait();
                return false;
            }

            CPacketHelperEx<20480> packet;
            std::array<ItemManip, 100> requests{};
            int requestCount = 0;
            if (useCount < maximumUseCount) {
                const double roll = genrand64_real1() * 100.0;
                double cumulativeProbability = 0.0;
                for (const CreateItemStructInUseCount& candidate : createItems) {
                    cumulativeProbability += candidate.prob_;
                    if (roll <= cumulativeProbability) {
                        requests[0] = ItemManip(0, candidate.classId_, candidate.count_, 0, 0, 0, true);
                        requestCount = 1;
                        if (!user->m_inv.CheckAddable(candidate.classId_, candidate.count_, false)) {
                            SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
                            resetWait();
                            return false;
                        }
                        break;
                    }
                }
            }
            if (useCount + 1 >= maximumUseCount) {
                const int consumedItemIndex = runtime::Int32FromBits(item.GetDBID());
                const int consumedItemClassId = item.GetClassId();
                requests[requestCount] = ItemManip(0, consumedItemClassId, -1, item.m_data->GetEnchanted(), 0, 0, true);
                requests[requestCount].mDBId = consumedItemIndex;
                ++requestCount;
            }

            const unsigned int useCountItemIndex = item.GetDBID();
            const int useCountUserDatabaseId = user->GetDBID();
            CETC_MANI_USE_COUNT useCountManipulation(
                static_cast<unsigned int>(user->m_nId), useCountUserDatabaseId, useCountItemIndex, useCount, maximumUseCount
            );
            static_cast<EtcMani&>(useCountManipulation).Assemble(packet);

            L2_GUARD(user->m_ManipulateQueueLock);
            User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
            User::ManipulateQueue queue = user->SaveManipulateQueue(
                ManipulateType::MANI_USE_COUNT, 0, requestCount, requests.data(), nullptr, 1, packet, false,
                ItemGetReason::UseCount, message
            );
            const User::ManiItemResultType result = user->_SetManiItemJob(queue);
            if (result == User::MRT_NOJOB) {
                if (!user->_HandleItemManipulation(queue)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[ERROR][%s][%d] MANI_USE_COUNT "
                    //     L"_HandleItemManipulation failed",
                    //     L"Creature_server.cpp",
                    //     0x1a5c)
                    // Log.Add(
                    //     LOG_WARN,
                    //     "[items] Use-count processing failed for object %u, item class %d, database ID %d (use count %d of
                    //     %d, %d requested item changes). Clearing the item-job wait flag.", m_nId, itemClassId,
                    //     itemDatabaseId,
                    //     static_cast<int>(useCount),
                    //     static_cast<int>(maximumUseCount),
                    //     requestCount)
                    L2_LOG_WARN(
                        Log,
                        "[items] Use-count processing failed for object %u, item class %d, database ID %d (use count %d of "
                        "%d, %d requested item changes); clearing the item-job wait flag.",
                        m_nId, itemClassId, itemDatabaseId, static_cast<int>(useCount), static_cast<int>(maximumUseCount),
                        requestCount
                    );
                    user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
                    resetWait();
                }
            } else if (result == User::MRT_JOBMAP) {
                user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                resetWait();
            } else if (result == User::MRT_JOBNOTMAP) {
                // Noise: MRT_JOBNOTMAP means the use-count operation was queued behind another item job. Successful queue
                // insertion is routine and does not prove item delivery. Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"Creature_server.cpp",
                //     0x1a6b)
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[ERROR][%s][%d] ManiItemQueue : default",
                //     L"Creature_server.cpp",
                //     0x1a70)
                // Log.Add(
                //     LOG_ERROR,
                //     "[items] Unexpected queue result %d for use-count item class %d, database ID %d, object %u (%d
                //     requested item changes), clearing the item-job wait flag.", static_cast<int>(result), itemClassId,
                //     itemDatabaseId,
                //     m_nId,
                //     requestCount)
                L2_LOG_ERROR(
                    Log,
                    "[items] Unexpected queue result %d for use-count item class %d, database ID %d, object %u (%d "
                    "requested item changes); clearing the item-job wait flag.",
                    static_cast<int>(result), itemClassId, itemDatabaseId, m_nId, requestCount
                );
                user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                resetWait();
            }

            if (requestCount != 0) {
                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x235c, user, nullptr, nullptr, nullptr, nullptr, useCount, maximumUseCount, requests[0].mClassId,
                    requests[0].mCount, 0, 0, static_cast<std::int64_t>(item.GetDBID())
                );
            }
            break;
        }

        case ITEM_ACTION_SHOW_ADVENTURER_GUIDE_BOOK:
        case ITEM_ACTION_BLESS_SOULSHOT:
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Can't use item. item class id[%d]",
            //     "Creature_server.cpp",
            //     0x1a84,
            //     itemClassId)
            // Log.Add(
            //     LOG_ERROR,
            //     "[items] Unsupported item action %d for object %u, item class %d, database ID %d. No action handler
            //     runs.", static_cast<int>(action), m_nId, itemClassId, itemDatabaseId)
            L2_LOG_ERROR(
                Log,
                "[items] Unsupported item action %d for object %u, item class %d, database ID %d; no action handler runs.",
                static_cast<int>(action), m_nId, itemClassId, itemDatabaseId
            );
            break;
    }

    return finish();
}

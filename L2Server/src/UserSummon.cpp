#include "CConfig.h"
#include "CWorld.h"
#include "Castle.h"
#include "CreatureController.h"
#include "Cubic.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Fishing.h"
#include "Item.h"
#include "MiniGameAgit.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadStuff.h"
#include "PCParam.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/net/Serialize.h"
#include "Skill.h"
#include "SkillFxFunc.h"
#include "ThreadLocalWorldMap.h"
#include "Trap.h"
#include "User.h"
#include "UserSocket.h"
#include "WorldCollision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

AtomicPetDBJob* g_AtomicPetDBJob = nullptr;

void InitializeAtomicPetDBJob() {
    // Current L2Server 0x00b17c38. There is no repeat guard:
    // the target startup calls this deferred dynamic initializer once and
    // publishes the newly allocated process-lifetime pointer directly.
    g_AtomicPetDBJob = new AtomicPetDBJob;
}

void User::ReserveSummon(int count, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SummonReservedLock);

    SummonReservedInfo summon{};
    for (int index = 0; index < count; ++index) {
        packet = ::Disassemble(
            packet, "ddddddffddd", &summon.m_nClassId, &summon.m_nDbId, &summon.m_nLevel, &summon.m_nOrgRemainTime,
            &summon.m_nRemainTime, &summon.m_nExpRate, &summon.m_dHP, &summon.m_dMP, &summon.m_nConsumeItemId,
            &summon.m_nConsumeItemCount, &summon.m_nIsSummon
        );
        m_vSummonReserved.push_back(summon);
        runtime::raw_atomic::ExchangeI32(&m_bNeedUpdateSummon, 1);
    }
}
bool User::HaveAgathion() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_spriteLock);
    return m_pAgathion != nullptr;
}
bool User::HaveAgathion(int agathionId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_spriteLock);
    return m_pAgathion != nullptr && agathionId == runtime::SubtractInt32Wrapping(m_pAgathion->m_npcClassID, 1000000);
}
void User::SetAgathionElapsedTime(int agathionId, int elapsedTime) {
    L2_GUARD_FUNCTION();

    if (elapsedTime >= 0) {
        L2_GUARD(m_spriteLock);
        if (m_pAgathion == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AGATHION] [User Summon Agathion ID : %d] "
            //     L"Agathion Null, %s, %s, %d",
            //     agathionId,
            //     GetName(),
            //     L"user_summon.cpp",
            //     0xf3f)
            // L2_LOG_ERROR(Log,
            //     L"[AGATHION] [User Summon Agathion ID : %d] "
            //     L"Agathion Null, %s, %s, %d",
            //     agathionId,
            //     GetName(),
            //     L"user_summon.cpp",
            //     0xf3f)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): cannot apply agathion %d elapsed time %d: current agathion is missing. Activation
            //     flag remains unchanged.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), agathionId,
            //     elapsedTime)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): cannot apply agathion %d elapsed time %d: current agathion is missing; activation "
                "flag remains unchanged.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), agathionId, elapsedTime
            );
            return;
        }

        if (m_pAgathion->m_classID == agathionId) {
            m_pAgathion->SetElapsedTimeToDailyTimeSkill(this, elapsedTime, m_pAgathionActivationInfo);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[AGATHION] [User Summon Agathion ID : %d] "
            //     L"Is Not Equal [Received Agathion ID From DB : %d] "
            //     L". %s, %s, %d",
            //     m_pAgathion->m_classID,
            //     agathionId,
            //     GetName(),
            //     L"user_summon.cpp",
            //     0xf45)
            // L2_LOG_ERROR(Log,
            //     L"[AGATHION] [User Summon Agathion ID : %d] "
            //     L"Is Not Equal [Received Agathion ID From DB : %d] "
            //     L". %s, %s, %d",
            //     m_pAgathion->m_classID,
            //     agathionId,
            //     GetName(),
            //     L"user_summon.cpp",
            //     0xf45)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): agathion elapsed-time reply targets class %d, current class is %d. Reply ignored and
            //     activation flag cleared.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), agathionId,
            //     m_pAgathion->m_classID)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): agathion elapsed-time reply targets class %d, current class is %d; reply ignored and "
                "activation flag cleared.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), agathionId, m_pAgathion->m_classID
            );
        }
    }

    runtime::raw_atomic::ExchangeI32(&m_lAgathionFlag, 0);
}
bool User::HaveSummonedNpc() {
    L2_GUARD_FUNCTION();
    const bool result = [&] {
        L2_GUARD(m_SummonedNpcLock);
        return !m_vSummonedNpcList.empty();
    }();
    return result;
}
int User::GetSummonCount() {
    L2_GUARD_FUNCTION();
    const int result = [&] {
        L2_GUARD(m_SummonLock);
        return static_cast<int>(m_mapSummonSID.size());
    }();
    return result;
}
int User::GetSummonIndex(int summonServerId) {
    L2_GUARD_FUNCTION();
    const int index = [&] {
        L2_GUARD(m_SummonLock);
        return _GetSummonIndex(summonServerId);
    }();
    return index;
}
void User::CopyUserBuffToNPC(CNPC* npc) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_InfectedSkillsSect);
        for (const InfectedSkillType& sourceSkill : m_InfectedSkills) {
            InfectedSkillType infectedSkill(sourceSkill);
            L2_GUARD(npc->m_InfectedSkillsSect);

            CSkillInfo* const skillInfo = infectedSkill.pSkillInfo;
            if (skillInfo == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     "InfectedSkillType's pSkillInfo should not be NULL "
                //     "[%s][%d]",
                //     "user_summon.cpp",
                //     0xbc1)
                // L2_LOG_ERROR(Log,
                //     "InfectedSkillType's pSkillInfo should not be NULL "
                //     "[%s][%d]",
                //     "user_summon.cpp",
                //     0xbc1)
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): cannot copy a buff to NPC object %u: skill info is null. Skipping this buff.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), static_cast<unsigned>(npc->m_nId))
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): cannot copy a buff to NPC object %u: skill info is null; skipping this buff.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(npc->m_nId)
                );
                continue;
            }
            if (skillInfo->m_nTransferAbnormal != 0 || skillInfo->m_eDebuff != DB_GOOD ||
                skillInfo->m_nAbnormalType == ABNS_ABILITY_CHANGE) {
                continue;
            }
            if (!npc->CanBeBuffed(skillInfo, this)) {
                continue;
            }

            bool alreadyInfected = false;
            for (const InfectedSkillType& existing : npc->m_InfectedSkills) {
                if (existing.pSkillInfo->m_nAbnormalType == skillInfo->m_nAbnormalType) {
                    alreadyInfected = true;
                    break;
                }
            }
            if (alreadyInfected) {
                continue;
            }

            npc->m_InfectedSkills.push_back(infectedSkill);
            npc->_AddAbnormalStatus(*skillInfo, nullptr, true, true, SPST_CASTING);
        }
    }

    npc->ValidateAllOnSIM();
    npc->CheckAbnormalVisualEffect();
    npc->ValidateChanged(VB_MAX);
}
void User::GetAllSummonedNpc(std::vector<CNPC*>& summonedNpcs) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_SummonedNpcLock);
        for (const int summonedNpcId : m_vSummonedNpcList) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(summonedNpcId);
            if (creature != nullptr && creature->IsNPC()) {
                summonedNpcs.push_back(creature->CastNPC());
            }
        }
    }
}

bool User::SummonPendingSummonOrPet(
    int classId,
    int databaseId,
    int,
    int originalRemainTime,
    int remainTime,
    int experienceRate,
    double hp,
    double mp,
    int consumeItemId,
    int consumeItemCount,
    int isSummon
) {
    // Current L2Server 0x00bba9e4.
    L2_GUARD_FUNCTION();
    const bool isPet = isSummon == 0 && databaseId >= 1;
    if (!L2SkillFunc::CanSummon(this, STGT_SELF, true, isPet)) {
        return false;
    }

    if (!isPet) {
        CNPC* const npc = g_ObjectDB.CreateNPC(classId, true);
        if (npc == nullptr || !npc->IsSummonOrPet()) {
            return false;
        }
        const bool result = SummonCreature(
            npc->CastSummon(), experienceRate, consumeItemId, static_cast<std::int64_t>(consumeItemCount),
            originalRemainTime, remainTime, nullptr, true, databaseId
        );
        npc->m_data->SetHP(hp);
        npc->m_data->SetMP(mp);
        return result;
    }

    if (m_bPetLinked) {
        return false;
    }
    CItem* const item = m_inv.GetByDBID(databaseId);
    if (item == nullptr || g_PetManager.IsSummonedPetCollar(item->GetDBID())) {
        return false;
    }
    return SummonPet(item, false);
}

void User::SummonAllPending() {
    // Current L2Server 0x00bbb014.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_SummonReservedLock);
    if (runtime::raw_atomic::LoadI32Relaxed(&m_bNeedUpdateSummon) == 0) {
        return;
    }

    for (const SummonReservedInfo& reserved : m_vSummonReserved) {
        if (reserved.m_nClassId <= 0) {
            continue;
        }
        if (SummonPendingSummonOrPet(
                reserved.m_nClassId, reserved.m_nDbId, reserved.m_nLevel, reserved.m_nOrgRemainTime, reserved.m_nRemainTime,
                reserved.m_nExpRate, reserved.m_dHP, reserved.m_dMP, reserved.m_nConsumeItemId, reserved.m_nConsumeItemCount,
                reserved.m_nIsSummon
            )) {
            CLogFormatter formatter{};
            formatter.WriteLog2_1(0x1fa6, this, nullptr, reserved.m_nClassId, 0, 0, 0, 0, 0, 0);
        }
    }
    ClearSummonReserved();
}

bool User::SummonAgathion(int agathionClassId, int level) {
    // Current L2Server 0x00bb91f4.
    L2_GUARD_FUNCTION();

    CSprite* const sprite = SpriteDB::Inst().GetInfo(SPRITE_AGATHION, agathionClassId, level);
    if (sprite == nullptr) {
        return false;
    }

    CAgathion* const previous = m_pAgathion;
    L2_GUARD(m_spriteLock);
    CAgathion* const agathion = sprite->CastAgathion();
    if (agathion == nullptr) {
        return false;
    }
    if (previous == agathion) {
        return false;
    }

    runtime::raw_atomic::ExchangeI32(&m_lAgathionFlag, 1);
    m_pAgathion = agathion;

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    m_pAgathionActivationInfo->Init(now, true);
    db.RequestSaveAgathionSummonTime(this, agathion->m_classID, now);
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x44);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_AGATHION});

    CLogFormatter formatter{};
    formatter.WriteLog2_1(
        0x76, this, nullptr, runtime::SubtractInt32Wrapping(agathion->m_npcClassID, 1000000), 0, 0, 0, 0, 0, 0
    );
    return true;
}

void User::DespawnAgathion(int reason) {
    // Current L2Server 0x00bb6bcc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_spriteLock);

    CAgathion* const agathion = m_pAgathion;
    if (agathion == nullptr) {
        return;
    }
    m_pAgathion = nullptr;

    std::map<int, const CSkillInfo*> skills;
    for (const int itemClassId : agathion->m_nItemClassIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object != nullptr) {
            GetSkillsFromItem(*static_cast<CItem*>(object), skills, CCT_ANY_CRYSTAL_LEVEL);
        }
    }
    for (const auto& entry : skills) {
        const CSkillInfo* const skillInfo = entry.second;
        if (skillInfo != nullptr &&
            (skillInfo->m_OperateType == SKILL_TOGGLE || skillInfo->m_OperateType == SKILL_GROUP_TOGGLE) &&
            IsToggleSkillOnOff(skillInfo->m_nSkillId)) {
            OnMagicSkillUsePacket(skillInfo->m_nSkillId, false, false);
        }
    }

    std::int32_t now = 0;
    CIOObject::SetTimeStamp(&now);
    db.RequestSaveAgathionDespawnTime(this, agathion->m_classID, now);
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x44);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_AGATHION});

    if (reason >= 0) {
        CLogFormatter formatter{};
        formatter.WriteLog2_1(
            0x77, this, nullptr, runtime::SubtractInt32Wrapping(agathion->m_npcClassID, 1000000), reason,
            runtime::SubtractInt32Wrapping(now, m_pAgathionActivationInfo->summonedTime), 0, 0, 0, 0
        );
    }
}

bool User::CanSummon(const bool summon, const bool pet) {
    // Current L2Server 0x00bb24d8. This body has no guard.
    unsigned int messageId = 0;
    if (!pet) {
        if (GetSummonCount() >= 10) {
            messageId = SystemMessageId::CANNOT_SUMMON_MULTIPLE_PETS;
        }
    } else if (m_nPetSpawnCommand != 0 || HavePet()) {
        messageId = SystemMessageId::PET_SUMMON_UNSUMMON_OR_RIDING_IN_PROGRESS;
    }

    if (messageId == 0) {
        if (IsInVehicle() || IsInAirShip() || IsInShuttle()) {
            messageId = SystemMessageId::CANNOT_SUMMON_PET_OR_SERVITOR_HERE;
        } else if (CMiniGameAgit::IsPlayer(this)) {
            messageId = SystemMessageId::CANNOT_SUMMON_PET_OR_SERVITOR_HERE;
        } else if (IsNowTrade() || m_data->m_StopMode == STP_STORE) {
            messageId = SystemMessageId::CANNOT_SUMMON_DURING_TRADE_OR_PRIVATE_STORE;
        } else {
            if (!summon) {
                if (m_data->m_bIsInCombatMode) {
                    messageId = SystemMessageId::CANNOT_SUMMON_DURING_COMBAT;
                } else if (m_DuelUserInfo.IsInPartyDuel()) {
                    return false;
                }
            }
            if (messageId == 0) {
                if (m_bFlyMoving) {
                    messageId = SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE;
                } else {
                    return m_data->m_TrainingState != 1;
                }
            }
        }
    }

    SendSystemMessage(messageId);
    return false;
}

void User::TurnOffSummonSoulShot() {
    L2_GUARD_FUNCTION();

    if (g_bUseAutoSoulShotUI) {
        for (int uiSlot = 2; uiSlot <= 3; ++uiSlot) {
            AutoSoulShotUi& state = m_AutoSoulShotUi[uiSlot];
            state.m_ItemClassID = 0;
            GetSocket()->Send("chddd", 0xfe, 0x0c, state.m_ItemClassID, state.m_IsActiveState, uiSlot);
        }
    }

    for (int grade = 14; grade <= 16; ++grade) {
        if (m_nAutoSoulshotFlags[grade] == 0) {
            return;
        }
        for (int index = 0; index < 32; ++index) {
            const int itemClassId = g_nSoulshotIDTables[grade][index];
            if (itemClassId == 0) {
                break;
            }
            TurnOffAutoSoulshot(itemClassId, 0, 4, true);
        }
    }
}

void User::DeleteSummonedNpc(const int summonedNpcId) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_SummonedNpcLock);
        const auto position = std::find(m_vSummonedNpcList.begin(), m_vSummonedNpcList.end(), summonedNpcId);
        if (position != m_vSummonedNpcList.end()) {
            if (summonedNpcId != 0) {
                for (std::uint32_t& id : m_data->m_nSummonedNpc) {
                    if (id == static_cast<std::uint32_t>(summonedNpcId)) {
                        id = 0;
                        break;
                    }
                }
            }
            m_vSummonedNpcList.erase(position);
        }
    }
}

bool User::GetSummonedNpcPos(const CSkillInfo* const skillInfo, FVector& position) {
    // Current L2Server 0x00bb0e08. Intentionally unguarded.
    if (skillInfo != nullptr) {
        if (skillInfo->m_TargetType == STGT_GROUND) {
            if (m_NonTargetSkillPos.IsEqual(FVector{})) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Try to use ground skill without target pos. "
                //     L"char_id[%s][%d] skill_id[%d]",
                //     GetName(),
                //     GetDBID(),
                //     skillInfo->m_nSkillId)
                // L2_LOG_ERROR(Log,
                //     L"Try to use ground skill without target pos. "
                //     L"char_id[%s][%d] skill_id[%d]",
                //     GetName(),
                //     GetDBID(),
                //     skillInfo->m_nSkillId)
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): cannot position a summon for ground skill %d level %d: target position is
                //     zero.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), skillInfo->m_nSkillId,
                //     skillInfo->m_nLevel)
                L2_LOG_ERROR(
                    Log,
                    "Character %u ('%s'): cannot position a summon for ground skill %d level %d: target position is zero.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), skillInfo->m_nSkillId,
                    skillInfo->m_nLevel
                );
                return false;
            }
            position = m_NonTargetSkillPos;
            return true;
        }
        if (skillInfo->m_TargetType == STGT_TARGET && m_spTarget.IsValid()) {
            CWorldObject* const target = m_spTarget.GetWorldObject();
            if (target != nullptr) {
                position = target->GetPos();
                return true;
            }
        }
    }
    position = m_data->GetPos();
    return true;
}

bool User::SummonNpc(
    const int classId, int count, const CSkillInfo* const skillInfo, const NpcSummonType summonType, const int distance
) {
    // Current L2Server 0x00bba150.
    L2_GUARD_FUNCTION();
    if (classId <= 1000000) {
        return false;
    }

    const char16_t* const npcName = g_ObjectDB.GetName(classId);
    if (classId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid npc name [%s] at file[%s], line[%d]",
        //     npcName,
        //     L"user_summon.cpp",
        //     0xa82)
        // L2_LOG_ERROR(Log,
        //     L"invalid npc name [%s] at file[%s], line[%d]",
        //     npcName,
        //     L"user_summon.cpp",
        //     0xa82)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot summon NPC '%s': invalid class %d.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcName ? WideToUtf8(npcName).c_str() :
        //     "<null>", classId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot summon NPC '%s': invalid class %d.", static_cast<unsigned>(GetDBID()),
            Utf16ToUtf8(GetName()).c_str(), npcName ? Utf16ToUtf8(npcName).c_str() : "<null>", classId
        );
        return false;
    }

    const wchar_t* const aiName = g_ObjectDB.GetAI(classId);
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]",
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot summon NPC class %d: AI is unspecified.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), classId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot summon NPC class %d: AI is unspecified.", static_cast<unsigned>(GetDBID()),
            Utf16ToUtf8(GetName()).c_str(), classId
        );
        return false;
    }

    FVector position{};
    if (!GetSummonedNpcPos(skillInfo, position)) {
        return false;
    }
    count = std::min(count, 0x40);
    for (int index = 0; index < count; ++index) {
        CNPC* const npc = g_ObjectDB.CreateNPC(classId, true);
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"There is no NPC named [%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xa9f)
            // L2_LOG_ERROR(Log,
            //     L"There is no NPC named [%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xa9f)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): NPC creation failed for '%s' (class %d), request index %d of %d. Earlier creations
            //     are retained.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcName ?
            //     WideToUtf8(npcName).c_str() : "<null>", classId, index, count)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): NPC creation failed for '%s' (class %d), request index %d of %d; earlier creations "
                "are retained.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                npcName ? Utf16ToUtf8(npcName).c_str() : "<null>", classId, index, count
            );
            return false;
        }
        if (npc->IsSummonOrPet()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Pet or summon cannot be summoned by this function "
            //     L"[%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xaa5)
            // L2_LOG_ERROR(Log,
            //     L"Pet or summon cannot be summoned by this function "
            //     L"[%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xaa5)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): NPC '%s' (class %d) is a pet or servitor. Deleting it because this summon operation
            //     does not accept that type.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcName ?
            //     WideToUtf8(npcName).c_str() : "<null>", classId)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): NPC '%s' (class %d) is a pet or servitor; deleting it because this summon operation "
                "does not accept that type.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                npcName ? Utf16ToUtf8(npcName).c_str() : "<null>", classId
            );
            npc->Delete();
            return false;
        }

        if (npc->IsDoppelganger()) {
            // npc->SetDoppelGangerType(1)
            npc->SetDoppelGangerType(DoppelGangerType::DECOY);
            if (m_PledgeGameUser.IsPlayer()) {
                const CSkillInfo* const doppelgangerSkill = g_SkillDB.GetSkillInfo(CSkillKey(0x1bcb, 1), nullptr);
                if (doppelgangerSkill != nullptr) {
                    npc->InfectedSkillBy(nullptr, doppelgangerSkill, nullptr, 0);
                }
            }
            CopyUserBuffToNPC_Reserve(npc);
            const double hpRate = m_data->m_dHP / m_data->m_dBaseHP;
            const double mpRate = m_data->m_dMP / m_data->m_dBaseMP;
            npc->m_data->SetHP(npc->m_data->m_dBaseHP * hpRate);
            npc->m_data->SetMP(npc->m_data->m_dBaseMP * mpRate);
            npc->SetHPByMasterRate_Reserve(hpRate);
            npc->SetMPByMasterRate_Reserve(mpRate);
        }

        SetAndSendNPCParameters(npc, summonType, position, distance, aiName, 0, 0);
        npc->OnCreated();
        if (!LinkSummonedNpc(npc)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Too much npc to be linked, char[%s][%d] npc[%s]",
            //     GetName(),
            //     GetDBID(),
            //     npc->GetName())
            // L2_LOG_ERROR(Log,
            //     L"Too much npc to be linked, char[%s][%d] npc[%s]",
            //     GetName(),
            //     GetDBID(),
            //     npc->GetName())
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): cannot link created NPC '%s' (object %u): limit 10 reached. NPC remains created.",
            //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), WideToUtf8(npc->GetName()).c_str(),
            //     static_cast<unsigned>(npc->m_nId))
            L2_LOG_ERROR(
                Log, "Character %u ('%s'): cannot link created NPC '%s' (object %u): limit 10 reached; NPC remains created.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(npc->GetName()).c_str(),
                static_cast<unsigned>(npc->m_nId)
            );
        }
        npc->SetAsSummonedNpc();
    }
    return true;
}

bool User::GetHallucinationPos(CCreature* target, FVector& position) {
    // Current L2Server 0x00bb0f48. Intentionally unguarded.
    position = target->GetMeleeAttackPosition(this);
    return CWorld::Inst().IsValidPosition(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y)
    );
}

bool User::CopyUserBuffToNPC_Reserve(CNPC* npc) {
    // Current L2Server 0x00bb9420.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid parameter pNPC",
        //     L"user_summon.cpp",
        //     0xb5c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid parameter pNPC",
        //     L"user_summon.cpp",
        //     0xb5c)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot reserve buff copying: target NPC is null.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot reserve buff copying: target NPC is null.", static_cast<unsigned>(GetDBID()),
            Utf16ToUtf8(GetName()).c_str()
        );
        return false;
    }

    if (!npc->IsDoppelganger() && (!npc->IsSummonOrPet() || npc->GetUserOrMaster() != this)) {
        return false;
    }

    {
        L2_GUARD(m_InfectedSkillsSect);
        if (!m_InfectedSkills.empty()) {
            {
                L2_GUARD(npc->m_InfectedSkillsSect);
                npc->m_InfectedSkillsOfMaster.clear();
                npc->m_InfectedSkillsOfMaster.reserve(m_InfectedSkills.size());
                npc->m_InfectedSkillsOfMaster.insert(
                    npc->m_InfectedSkillsOfMaster.end(), m_InfectedSkills.begin(), m_InfectedSkills.end()
                );
            }
        }
    }
    return true;
}

bool User::SummonCreature(
    const wchar_t* npcName,
    int masterExpRatio,
    const wchar_t* consumeItemName,
    std::int64_t consumeItemCount,
    int originalDuration,
    int summonLifeTime,
    CSkillInfo* skillInfo
) {
    // Current L2Server 0x00bbabfc.
    L2_GUARD_FUNCTION();

    const std::u16string targetConsumeItemName = target_utf16::FromHostCodeUnits(consumeItemName);
    const int consumeItemClassId = g_ObjectDB.GetClassIdFromName(targetConsumeItemName.c_str(), false);
    const std::u16string targetNpcName = target_utf16::FromHostCodeUnits(npcName);
    const int npcClassId = g_ObjectDB.GetClassIdFromName(targetNpcName.c_str(), false);
    if (npcClassId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid npc name [%S] at file[%s], line[%d]",
        //     npcName,
        //     "user_summon.cpp",
        //     0x2a2)
        // L2_LOG_ERROR(Log,
        //     "invalid npc name [%S] at file[%s], line[%d]",
        //     npcName,
        //     "user_summon.cpp",
        //     0x2a2)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot resolve servitor template '%s'.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str())
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot resolve servitor template '%s'.", static_cast<unsigned>(GetDBID()),
            Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str()
        );
        return false;
    }

    CNPC* const npc = g_ObjectDB.CreateNPC(npcClassId, true);
    if (npc != nullptr && npc->IsSummon()) {
        return SummonCreature(
            npc->CastSummon(), masterExpRatio, consumeItemClassId, consumeItemCount, originalDuration, summonLifeTime,
            skillInfo, false, 0
        );
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "There is no NPC named [%S] at file[%s], line[%d]",
    //     npcName,
    //     "user_summon.cpp",
    //     0x2aa)
    // L2_LOG_ERROR(Log,
    //     "There is no NPC named [%S] at file[%s], line[%d]",
    //     npcName,
    //     "user_summon.cpp",
    //     0x2aa)
    // L2_LOG_ERROR(Log,
    //     "Character %u ('%s'): cannot create servitor '%s' (class %d): NPC is missing or is not a servitor.",
    //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str(), npcClassId)
    L2_LOG_ERROR(
        Log, "Character %u ('%s'): cannot create servitor '%s' (class %d): NPC is missing or is not a servitor.",
        static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str(), npcClassId
    );
    return false;
}

bool User::SummonCreature(
    CSummon* summon,
    int masterExpRatio,
    int consumeItemClassId,
    std::int64_t consumeItemCount,
    int originalDuration,
    int summonLifeTime,
    CSkillInfo* skillInfo,
    bool fixedIndex,
    int summonIndex
) {
    // Current L2Server 0x00bb9b84.
    L2_GUARD_FUNCTION();

    if (summon->m_data->m_Race == RT_SIEGE_WEAPON) {
        CPledge* const pledge = GetPledge();
        if (pledge == nullptr) {
            summon->Delete();
            return false;
        }

        const int castleIndex = m_nCurCastleId;
        if ((pledge->GetCurrentCastleSiegeDbId(castleIndex) < 0 || pledge->GetCurrentCastleSiegeStatus(castleIndex) != 1) &&
            (m_data->m_nDominionSiegeId == 0 || m_data->m_nDominionSiegeStatus == 0)) {
            summon->Delete();
            return false;
        }
    }

    const int summonPoint = summon->GetSummonPoint();
    if (summonPoint > 0) {
        const std::uint32_t totalBits = static_cast<std::uint32_t>(m_nSummonPoint) + static_cast<std::uint32_t>(summonPoint);
        const std::int32_t total = runtime::Int32FromBits(totalBits);
        if (total > m_nMaxSummonPoint) {
            summon->Delete();
            return false;
        }
        m_nSummonPoint = total;
    }

    const wchar_t* const aiName = g_ObjectDB.GetAI(summon->GetNPCId());
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     summon->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]",
        //     summon->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot summon class %d: AI is unspecified. Deleting the new servitor, earlier
        //     summon-point update is retained.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
        //     summon->m_nClassId)
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): cannot summon class %d: AI is unspecified; deleting the new servitor, earlier "
            "summon-point update is retained.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), summon->m_nClassId
        );
        summon->Delete();
        return false;
    }

    if (GetSummonCount() > 9) {
        // Log.Add(
        //     LOG_ERROR,
        //     "summon exceeded max [%d/%d] [clsid=%d]",
        //     GetSummonCount(),
        //     10,
        //     summon->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "summon exceeded max [%d/%d] [clsid=%d]",
        //     GetSummonCount(),
        //     10,
        //     summon->m_nClassId)
        // L2_LOG_WARN(Log,
        //     "Character %u ('%s'): cannot add servitor class %d: already %d summons, limit 10. Deleting the new servitor.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), summon->m_nClassId, GetSummonCount())
        L2_LOG_WARN(
            Log,
            "Character %u ('%s'): cannot add servitor class %d: already %d summons, limit 10; deleting the new servitor.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), summon->m_nClassId, GetSummonCount()
        );
        summon->Delete();
        return false;
    }

    std::array<char, 0x5000> buffer{};
    int bufferLength = 0;
    int parameterCount = 0;
    for (const auto& parameter : summon->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "SS", parameter.first.c_str(),
            parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    {
        L2_GUARD(m_SummonLock);
        if (m_bIsObserving) {
            summon->Delete();
            return false;
        }

        if (!fixedIndex) {
            summonIndex = _MakeSummonIndex();
        } else if (summonIndex > 9) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid Summon[%d][%s] for user [%s]",
            //     summonIndex,
            //     summon->GetName(),
            //     GetName())
            // L2_LOG_ERROR(Log,
            //     L"Invalid Summon[%d][%s] for user [%s]",
            //     summonIndex,
            //     summon->GetName(),
            //     GetName())
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): cannot assign servitor '%s' to fixed index %d above 9. Deleting the new servitor.",
            //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), WideToUtf8(summon->GetName()).c_str(),
            //     summonIndex)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): cannot assign servitor '%s' to fixed index %d above 9; deleting the new servitor.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(summon->GetName()).c_str(),
                summonIndex
            );
            summon->Delete();
            return false;
        }

        m_mapSummonSID[summonIndex] = summon->m_nId;
        m_data->m_pSummon[summonIndex] = summon;
        summon->m_nIndexForMaster = summonIndex;
    }

    summon->m_data->m_SummonType = NST_SUMMON;
    summon->SetMasterName(GetName());
    summon->m_data->m_dwSummonLifeTime = static_cast<std::uint32_t>(summonLifeTime) * 1000U;
    summon->m_nOrgDuration = runtime::MultiplyInt32Wrapping(originalDuration, 1000);
    summon->m_nConsumedCount = 1;
    summon->SetMaster(this);
    summon->CacheMasterInfo(this);
    summon->m_nConsumeItemClassID = consumeItemClassId;
    summon->m_nConsumeItemCount = consumeItemCount;
    summon->m_data->m_nMasterExpRatio = masterExpRatio;
    summon->SetPrivateID(GetPrivateID());
    CopyUserBuffToNPC_Reserve(summon);

    const FVector position = summon->ChoiceValidPosition(this, skillInfo);
    NpcServer::NPCCreateParam parameter{};
    parameter.pNPC = summon;
    parameter.pos = position;
    parameter.aiName = const_cast<wchar_t*>(aiName);
    parameter.paramCount = parameterCount;
    parameter.bufLen = bufferLength;
    parameter.pBuf = buffer.data();
    // Current 0x00bba0d0..0x00bba0dc reads User vslot +0xb8
    // (CObject::GetInZoneID), not the unrelated DBID accessor.
    parameter.inzoneSID = static_cast<int>(GetInZoneID());
    NpcServer::Inst()->SendNPCCreatePacket(parameter);

    if (fixedIndex) {
        summon->OnCreated();
    }
    if (User* const master = summon->GetMaster()) {
        master->ValidateChanged(VB_SKILLMOD);
    }
    return true;
}

bool User::SummonNPCRelatedToPledge(const wchar_t* npcName, CSkillInfo* skillInfo) {
    // Current L2Server 0x00bb45e4.
    L2_GUARD_FUNCTION();

    if (GetPledge() == nullptr) {
        return false;
    }

    const std::u16string targetNpcName = target_utf16::FromHostCodeUnits(npcName);
    const int npcClassId = g_ObjectDB.GetClassIdFromName(targetNpcName.c_str(), false);
    if (npcClassId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid npc name [%S] at file[%s], line[%d]",
        //     npcName,
        //     "user_summon.cpp",
        //     0x374)
        // L2_LOG_ERROR(Log,
        //     "invalid npc name [%S] at file[%s], line[%d]",
        //     npcName,
        //     "user_summon.cpp",
        //     0x374)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot resolve clan-related NPC template '%s'.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str())
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot resolve clan-related NPC template '%s'.", static_cast<unsigned>(GetDBID()),
            Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(targetNpcName).c_str()
        );
        return false;
    }
    return SummonNPCRelatedToPledge(g_ObjectDB.CreateNPC(npcClassId, true), skillInfo);
}

bool User::SummonNPCRelatedToPledge(CNPC* npc, CSkillInfo* skillInfo) {
    // Current L2Server 0x00bb39b0.
    L2_GUARD_FUNCTION();

    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "There is no NPC id at file[%s], line[%d]",
        //     "user_summon.cpp",
        //     0x385)
        // L2_LOG_ERROR(Log,
        //     "There is no NPC id at file[%s], line[%d]",
        //     "user_summon.cpp",
        //     0x385)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot summon clan-related NPC: creation returned null.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot summon clan-related NPC: creation returned null.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str()
        );
        return false;
    }

    CPledge* const pledge = GetPledge();
    if (pledge == nullptr) {
        npc->Delete();
        return false;
    }

    const wchar_t* const aiName = g_ObjectDB.GetAI(npc->GetNPCId());
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     npc->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]",
        //     npc->m_nClassId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot summon clan-related NPC class %d: AI is unspecified. Deleting the new NPC.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npc->m_nClassId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot summon clan-related NPC class %d: AI is unspecified; deleting the new NPC.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), npc->m_nClassId
        );
        npc->Delete();
        return false;
    }

    std::array<char, 0x2000> buffer{};
    int bufferLength = 0;
    int parameterCount = 0;
    for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "SS", parameter.first.c_str(),
            parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    npc->m_data->m_bIsNPCRelatedToPledge = true;
    npc->m_data->SetPledgeID(pledge->GetDbId());
    npc->m_data->SetWorldId(pledge->GetWorldId());
    npc->m_data->m_nPledgeIndex = static_cast<std::uint32_t>(pledge->GetIndex());
    npc->m_data->m_nRelatedIndex = 0;
    npc->m_data->m_nResidenceID = 0;
    npc->m_data->m_nResidenceOwnerID = 0;
    npc->SetMasterName(std::u16string(pledge->GetPledgeName()).c_str());
    npc->m_data->m_SummonerID = m_nId;
    npc->m_data->m_SummonType = NST_NOTHING;
    npc->m_data->m_BossID = 0;
    npc->m_data->m_nMasterExpRatio = 0;
    // Current 0x00bb3c8c..0x00bb3ca0 copies User vslot +0xb8
    // (GetInZoneID) into NPC vslot +0xc0 (SetInZoneID).
    npc->SetInZoneID(GetInZoneID());

    const FVector position = npc->ChoiceValidPosition(this, skillInfo);
    NpcServer::NPCCreateParam parameter{};
    parameter.pNPC = npc;
    parameter.pos = position;
    parameter.aiName = const_cast<wchar_t*>(aiName);
    parameter.paramCount = parameterCount;
    parameter.bufLen = bufferLength;
    parameter.pBuf = buffer.data();
    // Current 0x00bb3d18..0x00bb3d24 reads the same +0xb8 zone-ID slot.
    parameter.inzoneSID = static_cast<int>(GetInZoneID());
    NpcServer::Inst()->SendNPCCreatePacket(parameter);
    npc->OnCreated();
    return true;
}

void User::SetAndSendNPCParameters(
    CNPC* npc, NpcSummonType summonType, FVector& position, int distance, const wchar_t* aiName, int hp, int mp
) {
    // Current L2Server 0x00bb3d6c.
    L2_GUARD_FUNCTION();

    std::array<char, 0x2400> buffer{};
    int bufferLength = 0;
    int parameterCount = 0;
    for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "SS", parameter.first.c_str(),
            parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    npc->SetMasterName(GetName());
    npc->SetMaster(this);
    npc->m_data->m_SummonerID = m_data->m_nId;
    npc->m_data->m_SummonType = summonType;
    if (summonType == NST_UNIQUE) {
        npc->m_data->m_bUnique = true;
    }

    const int originalDistance = distance;
    if (distance > 0) {
        const FVector source = GetPos();
        CWorld::Inst().GetValidPositionAroundPos(*this, distance, npc->m_data->m_dCollisionRadius, source, position);
        npc->m_data->SetPos(position);

        const double radians = std::atan2(source.y - position.y, source.x - position.x);
        const int direction = runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.28318530717958647692);
        npc->m_data->SetDirByInt(direction);
    }

    NpcServer::NPCCreateParam parameter{};
    parameter.pNPC = npc;
    parameter.pos = position;
    parameter.dir = originalDistance < 1 ? m_data->GetDirByInt() : npc->m_data->GetDirByInt();
    parameter.hp = hp;
    parameter.mp = mp;
    parameter.aiName = const_cast<wchar_t*>(aiName);
    parameter.paramCount = parameterCount;
    parameter.bufLen = bufferLength;
    parameter.pBuf = buffer.data();
    parameter.airshipSID = 0;
    parameter.inzoneSID = static_cast<int>(GetInZoneID());
    NpcServer::Inst()->SendNPCCreatePacket(parameter);
}

bool User::LinkSummonedNpc(CNPC* npc) {
    // Current L2Server 0x00bb72d8.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    {
        L2_GUARD(m_SummonedNpcLock);
        if (m_vSummonedNpcList.size() >= 10) {
            return false;
        }

        const std::uint32_t npcId = npc->Id();
        for (std::uint32_t& id : m_data->m_nSummonedNpc) {
            if (id == 0) {
                id = npcId;
                break;
            }
        }
        m_vSummonedNpcList.push_back(static_cast<std::int32_t>(npcId));

        CCreatureController* const controller = npc->GetController();
        if (controller != nullptr) {
            controller->Serializer.SetMasterName(GetName());
        }
    }
    return true;
}

bool User::SummonHallucination(int classId, int count, const CSkillInfo*, NpcSummonType summonType, CCreature* target) {
    // Current L2Server 0x00bba56c.
    L2_GUARD_FUNCTION();
    if (classId < 1000001) {
        return false;
    }

    const char16_t* const npcName = g_ObjectDB.GetName(classId);
    if (classId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid npc name [%s] at file[%s], line[%d]",
        //     npcName,
        //     L"user_summon.cpp",
        //     0xae3)
        // L2_LOG_ERROR(Log,
        //     L"invalid npc name [%s] at file[%s], line[%d]",
        //     npcName,
        //     L"user_summon.cpp",
        //     0xae3)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot create hallucination '%s': invalid class %d.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcName ? WideToUtf8(npcName).c_str() :
        //     "<null>", classId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot create hallucination '%s': invalid class %d.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
            npcName ? Utf16ToUtf8(npcName).c_str() : "<null>", classId
        );
        return false;
    }
    const wchar_t* const aiName = g_ObjectDB.GetAI(classId);
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]",
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot create hallucination class %d: AI is unspecified.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), classId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot create hallucination class %d: AI is unspecified.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), classId
        );
        return false;
    }
    if (count > 10) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Too many hallucination. %s - %d",
        //     npcName,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"Too many hallucination. %s - %d",
        //     npcName,
        //     count)
        // L2_LOG_WARN(Log,
        //     "Character %u ('%s'): cannot create %d hallucinations of '%s': limit is 10.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), count, npcName ? WideToUtf8(npcName).c_str()
        //     : "<null>")
        L2_LOG_WARN(
            Log, "Character %u ('%s'): cannot create %d hallucinations of '%s': limit is 10.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), count,
            npcName ? Utf16ToUtf8(npcName).c_str() : "<null>"
        );
        return false;
    }

    const bool relatedToPledge = GetPledge() != nullptr && !m_PledgeGameUser.IsPlayer();
    for (int index = 0; index < count; ++index) {
        FVector position{};
        if (!GetHallucinationPos(target, position) || !CWorld::Inst().CanSummonNpc(position, this)) {
            continue;
        }

        CNPC* const npc = g_ObjectDB.CreateNPC(classId, true);
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"There is no NPC named [%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xb08)
            // L2_LOG_ERROR(Log,
            //     L"There is no NPC named [%s] at file[%s], line[%d]",
            //     npcName,
            //     L"user_summon.cpp",
            //     0xb08)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): hallucination creation failed for '%s' (class %d), request index %d of %d. Earlier
            //     creations are retained.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcName ?
            //     WideToUtf8(npcName).c_str() : "<null>", classId, index, count)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): hallucination creation failed for '%s' (class %d), request index %d of %d; earlier "
                "creations are retained.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                npcName ? Utf16ToUtf8(npcName).c_str() : "<null>", classId, index, count
            );
            return false;
        }

        // npc->SetDoppelGangerType(2)
        npc->SetDoppelGangerType(DoppelGangerType::HALLUCINATION);
        CopyUserBuffToNPC_Reserve(npc);
        const double hpRate = m_data->m_dHP / m_data->m_dBaseHP;
        const double mpRate = m_data->m_dMP / m_data->m_dBaseMP;
        npc->m_data->SetHP(npc->m_data->m_dBaseHP * hpRate);
        npc->m_data->SetMP(npc->m_data->m_dBaseMP * mpRate);
        npc->SetHPByMasterRate_Reserve(hpRate);
        npc->SetMPByMasterRate_Reserve(mpRate);
        SetAndSendNPCParameters(
            npc, summonType, position, 0, aiName, runtime::Win64CvttDoubleToInt32(npc->m_data->m_dHP),
            runtime::Win64CvttDoubleToInt32(npc->m_data->m_dMP)
        );
        npc->OnCreated();
        if (!LinkSummonedNpc(npc)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Too much npc to be linked, %s",
            //     npc->GetName())
            // L2_LOG_ERROR(Log,
            //     L"Too much npc to be linked, %s",
            //     npc->GetName())
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): cannot link hallucination '%s' (object %u): limit 10 reached. NPC remains created.",
            //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), WideToUtf8(npc->GetName()).c_str(),
            //     static_cast<unsigned>(npc->m_nId))
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): cannot link hallucination '%s' (object %u): limit 10 reached; NPC remains created.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(npc->GetName()).c_str(),
                static_cast<unsigned>(npc->m_nId)
            );
        }
        npc->SetAsSummonedNpc();
        npc->m_data->m_bIsNPCRelatedToPledge = relatedToPledge;
    }
    return true;
}

bool User::DespawnPCTrap() {
    // Current L2Server 0x00bbadc4.
    L2_GUARD_FUNCTION();
    if (m_nPCTrapId != 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nPCTrapId);
        CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
        if (npc != nullptr && npc->IsPCTrap()) {
            npc->Suicide(DAMAGE_SUICIDE, 0);

            const FVector position = GetPos();
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
            //     0x1a4,
            //     GetDBID(),
            //     GetAccountID(),
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z),
            //     m_data->m_nClass,
            //     m_data->m_Race,
            //     m_data->m_Sex,
            //     m_data->m_nLevel,
            //     npc->GetNPCId(),
            //     npc->m_data->m_nLevel,
            //     GetName(),
            //     m_sAccountName.data())
            // Log.Add(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
            //     0x1a4,
            //     GetDBID(),
            //     GetAccountID(),
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z),
            //     m_data->m_nClass,
            //     m_data->m_Race,
            //     m_data->m_Sex,
            //     m_data->m_nLevel,
            //     npc->GetNPCId(),
            //     npc->m_data->m_nLevel,
            //     Utf16ToWide(GetName()).c_str(),
            //     m_sAccountName.data())
            Log.Add(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
                0x1a4, GetDBID(), GetAccountID(), static_cast<int>(position.x), static_cast<int>(position.y),
                static_cast<int>(position.z), m_data->m_nClass, m_data->m_Race, m_data->m_Sex, m_data->m_nLevel,
                npc->GetNPCId(), npc->m_data->m_nLevel, GetName(), m_sAccountName.data()
            );
            return false;
        }
        m_nPCTrapId = 0;
    }

    OnDespawnPCTrap(0);
    return false;
}

void User::SummonPCTrap(int classId) {
    // Current L2Server 0x00bbb1d4.
    L2_GUARD_FUNCTION();
    m_nPendingPCTrapClassId = classId;
    DespawnPCTrap();
}

void User::OnSpawnPCTrap(CPCTrap& trap) {
    L2_GUARD_FUNCTION();

    m_nPCTrapId = static_cast<std::int32_t>(trap.Id());
    const FVector position = GetPos();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
    //     0x1a3,
    //     GetDBID(),
    //     GetAccountID(),
    //     static_cast<int>(position.x),
    //     static_cast<int>(position.y),
    //     static_cast<int>(position.z),
    //     m_data->m_nClass,
    //     m_data->m_Race,
    //     m_data->m_Sex,
    //     m_data->m_nLevel,
    //     trap.GetNPCId(),
    //     trap.m_data->m_nLevel,
    //     GetName(),
    //     m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
    //     0x1a3,
    //     GetDBID(),
    //     GetAccountID(),
    //     static_cast<int>(position.x),
    //     static_cast<int>(position.y),
    //     static_cast<int>(position.z),
    //     m_data->m_nClass,
    //     m_data->m_Race,
    //     m_data->m_Sex,
    //     m_data->m_nLevel,
    //     trap.GetNPCId(),
    //     trap.m_data->m_nLevel,
    //     Utf16ToWide(GetName()).c_str(),
    //     m_sAccountName.data())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,,",
        0x1a3, GetDBID(), GetAccountID(), static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), m_data->m_nClass, m_data->m_Race, m_data->m_Sex, m_data->m_nLevel, trap.GetNPCId(),
        trap.m_data->m_nLevel, GetName(), m_sAccountName.data()
    );
}

void User::OnDespawnPCTrap(const long trapId) {
    L2_GUARD_FUNCTION();

    const std::int32_t id = static_cast<std::int32_t>(trapId);
    const std::int32_t observed = runtime::raw_atomic::CompareExchangeI32(&m_nPCTrapId, 0, id);
    if (observed != id) {
        return;
    }

    const std::int32_t pendingClassId = runtime::raw_atomic::ExchangeI32(&m_nPendingPCTrapClassId, 0);
    if (pendingClassId != 0) {
        SummonNpc(pendingClassId, 1, nullptr, NST_PCTRAP, 0);
    }
}

bool User::BeginSpawnPet(std::uint32_t petCollarSid, bool newlySummoned) {
    // Current L2Server 0x00bb362c.
    L2_GUARD_FUNCTION();

    CItem* const petCollar = ItemSmartIdManager()->GetObjectW(petCollarSid);
    if (petCollar == nullptr) {
        return false;
    }
    if (runtime::raw_atomic::CompareExchangeI32(&m_nPetSpawnCommand, 1, 0) != 0) {
        return false;
    }

    runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, 0);
    runtime::raw_atomic::ExchangeI32(&m_nPetDespawnSteps, 0);
    m_nPetCollarSID = petCollarSid;
    m_bNewlySummon = newlySummoned;

    CPet* const pet = GetPet();
    if (pet == nullptr) {
        db.WithdrawPet(this, petCollar, newlySummoned);
        runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, 6);
        return true;
    }

    runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, 1);
    m_jobOnDespawnPet = DESPAWN_PET_NONE;
    db.RequestDeleteSummon(*this, static_cast<int>(pet->GetDBID()), g_AtomicPetDBJob);
    pet->SaveInfectedSkills(g_AtomicPetDBJob);
    db.DepositPet(this, pet, false, g_AtomicPetDBJob);
    return true;
}

bool User::SummonPet(CItem* petCollar, bool newlySummoned) {
    // Current L2Server 0x00bb381c.
    L2_GUARD_FUNCTION();

    if (m_nPetSpawnCommand != 0 || IsRiding()) {
        SendSystemMessage(SystemMessageId::PET_SUMMON_UNSUMMON_OR_RIDING_IN_PROGRESS);
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        return false;
    }

    if (IsInAirShip()) {
        SendSystemMessage(SystemMessageId::CANNOT_SUMMON_PET_OR_SERVITOR_WHILE_ABOARD_VEHICLE);
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        return false;
    }
    if (IsTransformed(TransformType::Flying) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_SUMMON_PET_OR_SERVITOR_WHILE_ABOARD_VEHICLE);
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        return false;
    }
    if (m_data->m_TrainingState == 1) {
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        return false;
    }

    if (BeginSpawnPet(petCollar->Id(), newlySummoned)) {
        return true;
    }

    SendSystemMessage(SystemMessageId::PET_SUMMON_UNSUMMON_OR_RIDING_IN_PROGRESS);
    runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    return false;
}

void User::DespawnPet(bool first, bool second, JobOnDespawnPet job, bool complete) {
    L2_GUARD_FUNCTION();

    CPet* const pet = GetPet();
    if (pet == nullptr) {
        if (!first) {
            SendSystemMessage(SystemMessageId::NO_PET);
        } else {
            m_jobOnDespawnPet = job;
            OnDespawnPet();
        }
        return;
    }

    if (first && !pet->IsAlive() && !second) {
        return;
    }
    if (!BeginDespawnPet()) {
        SendSystemMessage(SystemMessageId::PET_SUMMON_UNSUMMON_OR_RIDING_IN_PROGRESS);
        return;
    }

    m_jobOnDespawnPet = job;
    if (!first) {
        if (pet->m_data->m_bIsInCombatMode) {
            SendSystemMessage(SystemMessageId::CANNOT_UNSUMMON_PET_IN_COMBAT);
            EndDespawnPet();
            return;
        }
        if (pet->Hungry()) {
            SendSystemMessage(
                pet->IsPremiumPet() ? SystemMessageId::CANNOT_UNSUMMON_MINION_PET_DEPARTURE_TIME_NEAR
                                    : SystemMessageId::CANNOT_UNSUMMON_HUNGRY_PET
            );
            EndDespawnPet();
            return;
        }
        db.RequestDeleteSummon(*this, static_cast<int>(pet->GetDBID()), g_AtomicPetDBJob);
        pet->DeleteInfectedSkills(g_AtomicPetDBJob);
    } else {
        OnPetStep(4);
        pet->SaveInfectedSkills(g_AtomicPetDBJob);
    }
    db.DepositPet(this, pet, complete, g_AtomicPetDBJob);
}

void User::ResetPet(int petCollarDatabaseId) {
    // Current L2Server 0x00bb44a8.
    L2_GUARD_FUNCTION();
    m_PetSID = 0;
    {
        L2_GUARD(m_SummonLock);
        m_mapSummonSID.clear();
    }
    m_data->m_pPet.id = -1;
    m_data->m_pPet.index = -1;

    CItem* const collar = m_inv.GetByDBID(petCollarDatabaseId);
    if (collar != nullptr && collar->IsPetCollarItem()) {
        CEtcItem* const etcItem = collar->CastEtcItem();
        if (etcItem != nullptr) {
            etcItem->m_bPetCollarTradable = true;
        }
    }
}

void User::_UnlinkSummon(CSummon* summon) {
    // Current L2Server.exe 0x00bb4714, original owner user_summon.cpp.
    L2_GUARD_FUNCTION();

    const int index = _GetSummonIndex(static_cast<int>(summon->Id()));
    if (static_cast<unsigned int>(index) < 10U) {
        m_data->m_pSummon[index].id = -1;
        m_data->m_pSummon[index].index = -1;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid summon index! %d [%d][%s]",
        //     index,
        //     static_cast<int>(summon->Id()),
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     L"Invalid summon index! %d [%d][%s]",
        //     index,
        //     static_cast<int>(summon->Id()),
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot clear shared summon slot %d for object %u: index outside 0..9. Internal map
        //     unlink continues.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), index,
        //     static_cast<unsigned>(summon->Id()))
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): cannot clear shared summon slot %d for object %u: index outside 0..9; internal map unlink "
            "continues.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), index, static_cast<unsigned>(summon->Id())
        );
    }

    const auto position = m_mapSummonSID.find(index);
    if (position != m_mapSummonSID.end()) {
        m_mapSummonSID.erase(position);
    }
    if (m_mapSummonSID.empty()) {
        m_data->m_nSummonAI = 0;
    }
    if (summon->GetSummonPoint() > 0) {
        DecreaseSummonPointByDespawn(summon);
        summon->SendSummonInfo(true, false);
    }
}

void User::UnlinkSummon(CSummon* summon) {
    // Current L2Server.exe 0x00bb4988, original owner user_summon.cpp.
    L2_GUARD_FUNCTION();

    const int index = GetSummonIndex(static_cast<int>(summon->Id()));
    if (static_cast<unsigned int>(index) < 10U) {
        m_data->m_pSummon[index].id = -1;
        m_data->m_pSummon[index].index = -1;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid summon index! %d [%d][%s]",
        //     index,
        //     static_cast<int>(summon->Id()),
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     L"Invalid summon index! %d [%d][%s]",
        //     index,
        //     static_cast<int>(summon->Id()),
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot clear shared summon slot %d for object %u: index outside 0..9. Map unlink
        //     continues under the summon lock.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), index,
        //     static_cast<unsigned>(summon->Id()))
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): cannot clear shared summon slot %d for object %u: index outside 0..9; map unlink "
            "continues under the summon lock.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), index, static_cast<unsigned>(summon->Id())
        );
    }

    {
        L2_GUARD(m_SummonLock);
        const auto position = m_mapSummonSID.find(index);
        if (position != m_mapSummonSID.end()) {
            m_mapSummonSID.erase(position);
        }
        if (m_mapSummonSID.empty()) {
            m_data->m_nSummonAI = 0;
        }
        if (summon->GetSummonPoint() > 0) {
            DecreaseSummonPointByDespawn(summon);
            summon->SendSummonInfo(true, false);
        }
    }
}

void User::DespawnSummon(CSummon* summon, bool force, bool saveInfectedSkills) {
    L2_GUARD_FUNCTION();

    if (summon == nullptr) {
        return;
    }
    if (!force && summon->m_data->m_bIsInCombatMode) {
        GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_UNSUMMON_SERVITOR_IN_COMBAT);
        return;
    }

    const int index = GetSummonIndex(static_cast<int>(summon->Id()));
    if (!saveInfectedSkills) {
        db.RequestDeleteSummon(*this, index, nullptr);
        summon->DeleteInfectedSkills(nullptr);
    } else {
        summon->SaveInfectedSkills(nullptr);
    }
    UnlinkSummon(summon);
    summon->Delete();
}

void User::DespawnAllSummon(const bool force, const bool saveInfectedSkills) {
    // Current L2Server.exe 0x00bb7710, original owner user_summon.cpp.
    L2_GUARD_FUNCTION();

    std::vector<CSummon*> despawned;
    std::vector<std::pair<std::int32_t, std::uint32_t>> summonIds;
    {
        L2_GUARD(m_SummonLock);
        summonIds.reserve(m_mapSummonSID.size());
        for (const auto& entry : m_mapSummonSID) {
            summonIds.push_back(entry);
        }

        for (const auto& entry : summonIds) {
            const std::int32_t index = entry.first;
            const std::uint32_t summonSid = entry.second;
            if (static_cast<std::uint32_t>(index) >= 10U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): bulk despawn skips summon object %u at index %d outside 0..9.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid),
                //     index)
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): bulk despawn skips summon object %u at index %d outside 0..9.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid), index
                );
                continue;
            }

            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(summonSid);
            if (creature == nullptr || !creature->IsSummon()) {
                const auto position = m_mapSummonSID.find(index);
                if (position != m_mapSummonSID.end()) {
                    m_mapSummonSID.erase(position);
                }
                continue;
            }

            if (!force && creature->m_data->m_bIsInCombatMode) {
                GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_UNSUMMON_SERVITOR_IN_COMBAT);
                continue;
            }
            if (!saveInfectedSkills) {
                db.RequestDeleteSummon(*this, index, nullptr);
                creature->CastSummon()->_DeleteInfectedSkills();
            } else {
                creature->CastSummon()->_SaveInfectedSkills();
            }
            _UnlinkSummon(creature->CastSummon());
            despawned.push_back(creature->CastSummon());
        }
    }

    for (CSummon* const summon : despawned) {
        if (summon != nullptr) {
            summon->Delete();
        }
    }
}

CSummon* User::GetOneSummon(OneSummonType summonType) {
    // Current L2Server 0x00bb66d0.
    L2_GUARD_FUNCTION();
    CSummon* result;
    {
        L2_GUARD(m_SummonLock);

        result = nullptr;
        for (const auto& entry : m_mapSummonSID) {
            const std::int32_t index = entry.first;
            const std::uint32_t summonSid = entry.second;
            if (static_cast<std::uint32_t>(index) >= 10U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): summon lookup skips object %u at index %d outside 0..9.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid),
                //     index)
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): summon lookup skips object %u at index %d outside 0..9.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid), index
                );
                continue;
            }

            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(summonSid);
            if (creature == nullptr || !creature->IsSummon()) {
                continue;
            }

            if (summonType == SUMMON_MULTI && creature->CastSummon()->GetSummonPoint() <= 0) {
                continue;
            }
            if (summonType == SUMMON_SINGLE && creature->CastSummon()->GetSummonPoint() >= 1) {
                continue;
            }
            if (creature->CastSummon()->GetMaster() == this) {
                result = creature->CastSummon();
                break;
            }
        }
    }
    return result;
}

CSummon* User::GetOneSummonLive(OneSummonType summonType) {
    // Current L2Server 0x00bb6944.
    L2_GUARD_FUNCTION();
    CSummon* result;
    {
        L2_GUARD(m_SummonLock);

        result = nullptr;
        for (const auto& entry : m_mapSummonSID) {
            const std::int32_t index = entry.first;
            const std::uint32_t summonSid = entry.second;
            if (static_cast<std::uint32_t>(index) >= 10U) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     L"Invalid summon index! %d [%d][%s]",
                //     index,
                //     summonSid,
                //     GetName())
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): live summon lookup skips object %u at index %d outside 0..9.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid),
                //     index)
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): live summon lookup skips object %u at index %d outside 0..9.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(summonSid), index
                );
                continue;
            }

            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(summonSid);
            if (creature == nullptr || !creature->IsSummon() || !creature->IsAlive()) {
                continue;
            }

            if (summonType == SUMMON_MULTI && creature->CastSummon()->GetSummonPoint() <= 0) {
                continue;
            }
            if (summonType == SUMMON_SINGLE && creature->CastSummon()->GetSummonPoint() >= 1) {
                continue;
            }
            if (creature->CastSummon()->GetMaster() == this) {
                result = creature->CastSummon();
                break;
            }
        }
    }
    return result;
}

void User::GetAllSummon(std::vector<CSummon*>& summons) {
    // Current L2Server 0x00bb7adc.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_SummonLock);
        for (const auto& entry : m_mapSummonSID) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(entry.second);
            if (creature == nullptr || !creature->IsSummon()) {
                continue;
            }
            if (creature->CastSummon()->GetMaster() == this) {
                summons.push_back(creature->CastSummon());
            }
        }
    }
}

int User::GetStriderClassID() { return m_pRidingInfo->StriderClassID(); }

bool User::CanRide(CPet* pet, bool sendMessage) {
    // Current L2Server 0x00bb8944.
    L2_GUARD_FUNCTION();

    unsigned int messageId = 0;
    if (!m_SkillMod.m_bCanAct) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_PETRIFIED;
    } else if (m_data->m_bIsAlive == 0) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_DEAD;
    } else if (m_pFishingController != nullptr &&
        (m_pFishingController->m_FishingState != FISHINGS_NONE || m_pFishingController->m_bIsAutoFishing)) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_FISHING;
    } else if (m_data->m_bIsInCombatMode) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_IN_COMBAT;
    } else if (IsInDuel()) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_IN_DUEL;
    } else if (!m_data->m_bIsActive) {
        messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_SITTING;
    } else {
        static_cast<void>(CCursedWeaponMgr::inst());
        if (m_nCursedWeaponClassId > 0) {
            messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WITH_CURSED_WEAPON;
        } else if (IsTransformedExceptModeChange() != nullptr) {
            messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_TRANSFORMED;
        } else if (IsEquippedWithForcedItem(ST_RLHAND)) {
            messageId = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_CARRYING_FLAG;
        }
    }

    if (messageId != 0) {
        if (sendMessage) {
            SendSystemMessage(messageId);
        }
        return false;
    }
    if (pet == nullptr) {
        return true;
    }
    if (pet->GetRideType() <= RIDE_NONE || !pet->mTrLock.Begin()) {
        return false;
    }
    if (pet->m_data->m_bIsAlive == 0 || pet->m_data->m_bIsInCombatMode) {
        pet->mTrLock.End();
        return false;
    }
    if (!CWorld::Inst().GetGeo()->CanSee(this, pet, IsPC())) {
        pet->mTrLock.End();
        SendSystemMessage(SystemMessageId::TARGET_NOT_VISIBLE);
        return false;
    }

    const FVector petPosition = pet->GetPos();
    const FVector userPosition = GetPos();
    const double deltaX = userPosition.x - petPosition.x;
    const double deltaY = userPosition.y - petPosition.y;
    if (deltaX * deltaX + deltaY * deltaY > 5476.0) {
        pet->mTrLock.End();
        GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_MOUNT_TOO_FAR);
        return false;
    }
    return true;
}

bool User::CanRideForEvent(int npcClassId) {
    // Current L2Server 0x00bb8f34.
    L2_GUARD_FUNCTION();
    if (!CanRide(nullptr, true)) {
        return false;
    }
    if (IsRiding()) {
        SendSystemMessage(SystemMessageId::ALREADY_ABOARD_ANOTHER_VEHICLE);
        return false;
    }
    if (HavePet() || HaveSummon()) {
        SendSystemMessage(SystemMessageId::CANNOT_BOARD_VEHICLE_WITH_PET_OR_SERVITOR_SUMMONED);
        return false;
    }

    CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
    if (object != nullptr && object->IsPet()) {
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid wyvern class id[%d]",
    //     L"user_summon.cpp",
    //     0x80a,
    //     npcClassId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid wyvern class id[%d]",
    //     L"user_summon.cpp",
    //     0x80a,
    //     npcClassId)
    // L2_LOG_ERROR(Log,
    //     "Character %u ('%s'): cannot validate wyvern riding: NPC class %d is missing or is not a pet.",
    //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcClassId)
    L2_LOG_ERROR(
        Log, "Character %u ('%s'): cannot validate wyvern riding: NPC class %d is missing or is not a pet.",
        static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), npcClassId
    );
    return false;
}

void User::CheckSummonBattleMode() {
    // Current L2Server.exe 0x00bb80b8.
    L2_GUARD_FUNCTION();
    if (!HavePet() && !HaveSummon()) {
        return;
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        this, summon,
        if (!m_data->m_bIsInCombatMode) {
            if (summon->m_data->m_bIsInCombatMode) {
                summon->SetInCombatMode(false);
                summon->BroadcastCombatModeFinish();
            }
        } else if (!summon->m_data->m_bIsInCombatMode) {
            summon->SetInCombatMode(true);
            summon->BroadcastCombatModeStart();
        }
    );
}

void User::CheckSummonBattleTimeStamp() {
    // Current L2Server.exe 0x00bb8314.
    L2_GUARD_FUNCTION();
    if (!HavePet() && !HaveSummon()) {
        return;
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        this, summon, if (!summon->m_data->m_bIsInCombatMode) {
            summon->BroadcastCombatModeStart();
            summon->SetInCombatMode(true);
        } CIOObject::SetTimeStamp(&summon->m_nCombatModeTimeStamp);
    );
}

void User::RidePet(CPet* pet) {
    // Current L2Server 0x00bb4b58.
    L2_GUARD_FUNCTION();

    m_data->m_rideLevel = m_pRidingInfo->Level();
    m_data->SetRideType(pet->GetRideType());
    m_data->SetRideClassID(pet->m_data->m_nClassID);
    m_data->SetCollisionRadius(pet->m_data->m_dCollisionRadius);
    m_data->SetCollisionHeight(pet->m_data->m_dCollisionHeight);
    if (!IsRidingOnGround()) {
        m_data->SetEnvironment(ENV_AIR);
        m_data->m_moveType = CMT_FLY;
    } else {
        m_data->SetEnvironment(m_AreaMod.m_bIsUnderWater ? ENV_UNDERWATER : ENV_GROUND);
    }

    DeleteAcquireSkill(8);
    DeleteAcquireSkill(7);
    DeleteAcquireSkill(14);
    m_data->m_bCanMove = true;
    DeleteYongmaAbnormalStatus(true);

    const FVector position = GetPos();
    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborRect(
            this, 0x80000, position, 0x800, "cddddddd", 0x8c, static_cast<int>(Id()), 1,
            static_cast<int>(m_data->m_rideType), m_data->m_rideClassID, static_cast<int>(position.x),
            static_cast<int>(position.y), static_cast<int>(position.z)
        );
    } else {
        GetSocket()->Send(
            "cddddddd", 0x8c, static_cast<int>(Id()), 1, static_cast<int>(m_data->m_rideType), m_data->m_rideClassID,
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
        );
    }
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PET});
    ValidateChanged(VB_MAX);
    m_pRidingInfo->MakeLog(0x73, this);
    runtime::raw_atomic::ExchangeI32(&m_initRidingInfo, 0);
}

bool User::RideWyvern(CPet* pet) {
    // Current L2Server 0x00bb4f2c.
    L2_GUARD_FUNCTION();
    if (pet->GetRideType() != RIDE_WYVERN) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pet[%d] is not wyvern type.[%d]",
        //     L"user_summon.cpp",
        //     0x7d1,
        //     pet->m_data->m_nClassID,
        //     static_cast<int>(pet->GetRideType()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] pet[%d] is not wyvern type.[%d]",
        //     L"user_summon.cpp",
        //     0x7d1,
        //     pet->m_data->m_nClassID,
        //     static_cast<int>(pet->GetRideType()))
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot ride pet class %d as a wyvern: ride type is %d.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), pet->m_data->m_nClassID,
        //     static_cast<int>(pet->GetRideType()))
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot ride pet class %d as a wyvern: ride type is %d.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), pet->m_data->m_nClassID,
            static_cast<int>(pet->GetRideType())
        );
        return false;
    }
    if (!IsAlive()) {
        GetSocket()->SendSystemMessage(SystemMessageId::MUST_RIDE_STRIDER_TO_MOUNT_WYVERN);
        return false;
    }
    if (IsEquippedWithForcedItem(ST_RLHAND)) {
        return false;
    }

    m_pRidingInfo->RideWyvern(pet, m_data->m_dCollisionRadius, m_data->m_dCollisionHeight);
    DespawnAllCubic(false);
    RidePet(pet);
    AcquireSkill(0x10c1, 1, 0, false, true);
    ValidateChanged(VB_SKILLLIST);
    return true;
}

bool User::RideWyvern(const int npcClassId) {
    // Current L2Server 0x00bb5f8c.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
    if (object != nullptr && object->IsPet()) {
        return RideWyvern(object->CastPet());
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid wyvern class id",
    //     L"user_summon.cpp",
    //     0x7c0)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid wyvern class id",
    //     L"user_summon.cpp",
    //     0x7c0)
    // L2_LOG_ERROR(Log,
    //     "Character %u ('%s'): cannot ride wyvern class %d: template is missing or is not a pet.",
    //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), npcClassId)
    L2_LOG_ERROR(
        Log, "Character %u ('%s'): cannot ride wyvern class %d: template is missing or is not a pet.",
        static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), npcClassId
    );
    return false;
}

bool User::RideForEvent(int npcClassId, int durationMinutes, int eventId) {
    // Current L2Server 0x00bb50c8.
    L2_GUARD_FUNCTION();

    CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
    if (object == nullptr || !object->IsPet()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid class id[%d]",
        //     L"user_summon.cpp",
        //     0x823,
        //     npcClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid class id[%d]",
        //     L"user_summon.cpp",
        //     0x823,
        //     npcClassId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot start event ride %d: NPC class %d is missing or is not a pet.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), eventId, npcClassId)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot start event ride %d: NPC class %d is missing or is not a pet.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), eventId, npcClassId
        );
        return false;
    }

    CPet* const pet = object->CastPet();
    int maximumLevel = 50;
    if (pet->GetRideType() == RIDE_WYVERN) {
        maximumLevel = 56;
    }
    const int level = m_data->m_nLevel <= maximumLevel ? m_data->m_nLevel : maximumLevel;
    m_pRidingInfo->RideForEvent(pet, level, m_data->m_dCollisionRadius, m_data->m_dCollisionHeight, durationMinutes);
    RidePet(pet);
    if (eventId > 0) {
        m_pRidingInfo->MakeEventLog(this, eventId);
    }
    return true;
}

void User::EatStriderFood(int delta) {
    // Current L2Server 0x00bb6094.
    L2_GUARD_FUNCTION();

    if (!IsRiding() || m_pRidingInfo->IsEventRiding()) {
        return;
    }

    const bool changed = m_pRidingInfo->Eat(delta);
    ShowStriderHungryGauge();
    ValidateSpeed();
    ValidateAttackSpeedAndTime();
    if (changed) {
        ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PET});
    }
    db.SaveStriderMeal(this, *m_pRidingInfo);
    if (m_pRidingInfo->IsMealEmpty()) {
        TryDismounting(true, true);
        SendSystemMessage(SystemMessageId::DISMOUNTED_NO_FEED);
    }
}

bool User::CheckRidingOnTimer() {
    // Current L2Server 0x00bb61fc.
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::CompareExchangeI32(&m_initRidingInfo, 0, 1) == 1) {
        m_pRidingInfo->Init();
        return true;
    }
    if (m_data->m_bIsAlive == 0 || !IsRiding()) {
        return false;
    }

    if (m_pRidingInfo->IsMealTime()) {
        const int consumeAmount = m_pRidingInfo->ConsumeAmount(m_data->m_bIsInCombatMode);
        EatStriderFood(runtime::Int32FromBits(0U - static_cast<std::uint32_t>(consumeAmount)));

        if (m_pRidingInfo->IsHungry()) {
            for (std::size_t index = 0; index < 4; ++index) {
                const PetDataSheet::Record& record = m_pRidingInfo->Record();
                const int foodClassId = record.m_nFoodClassId[index];
                if (foodClassId == 0) {
                    break;
                }

                CItem* const food = m_inv.GetFirstItemByClassID(foodClassId, false, false);
                if (food != nullptr) {
                    static_cast<void>(UseItem(*food, 0, false, 0));
                    break;
                }
                if (!m_pRidingInfo->IsHungry()) {
                    break;
                }
            }
        }
    }
    return CheckWyvernOnTimer();
}

bool User::Ride(CPet* pet) {
    // Current L2Server 0x00bb8cb0.
    L2_GUARD_FUNCTION();

    if (!CanRide(pet, false)) {
        return false;
    }
    if (GetController()->GetCurrentActionType() != ACT_SKILL) {
        const bool collarReady = [&] {
            L2_READ_GUARD(m_inv);
            CItem* const collar = m_inv.GetByDBID(pet->m_data->m_nPetDBID);
            if (collar == nullptr || !collar->IsEtcItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] user[%s] failed to ride.  no collar[%d]",
                //     L"user_summon.cpp",
                //     0x78c,
                //     GetName(),
                //     pet->m_data->m_nPetDBID)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] user[%s] failed to ride.  no collar[%d]",
                //     L"user_summon.cpp",
                //     0x78c,
                //     GetName(),
                //     pet->m_data->m_nPetDBID)
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): cannot mount: collar item %u is missing or is not an etc item.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
                //     static_cast<unsigned>(pet->m_data->m_nPetDBID))
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): cannot mount: collar item %u is missing or is not an etc item.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                    static_cast<unsigned>(pet->m_data->m_nPetDBID)
                );
                return false;
            }
            collar->CastEtcItem()->m_bPetCollarTradable = false;
            return true;
        }();
        if (collarReady) {
            if (m_pRidingInfo->RideStrider(pet, m_data->m_dCollisionRadius, m_data->m_dCollisionHeight)) {
                CWorld::Inst().MoveTo(this, pet->GetPos(), false);
                RidePet(pet);
                DespawnPet(false, true, DESPAWN_PET_NONE, false);
                ToggleSkillAllOff(true);
                AuraSkillAllOff(true);
                SendItemList(true, true, nullptr);
                ShowStriderHungryGauge();
                return true;
            }
            GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_MOUNT_OR_DISMOUNT_HUNGRY_MOUNT);
        }
    }
    pet->mTrLock.End();
    return false;
}

bool User::TryRiding() {
    // Current L2Server 0x00bb9090.
    L2_GUARD_FUNCTION();

    if (!IsAlive() || IsRiding() || m_data->m_TrainingState == 1) {
        return false;
    }
    if (IsInAirShip() || IsInVehicle() || IsInShuttle()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_ABOARD);
        return false;
    }
    CPet* const pet = GetPet();
    if (pet == nullptr) {
        SendSystemMessage(SystemMessageId::NO_PET);
        return false;
    }
    if (m_nPetSpawnCommand != 0) {
        SendSystemMessage(SystemMessageId::CANNOT_RIDE_DURING_PET_SUMMON_OR_UNSUMMON);
        return false;
    }
    return Ride(pet);
}

bool User::CanDismountOnAir(bool force, bool& restart) {
    L2_GUARD_FUNCTION();

    FVector position = GetPos();
    CGeoData* const geo = CWorld::Inst().GetGeo();
    CGeoZone* const zone = geo->GetZone(
        ((static_cast<int>(position.x) + 0x50000) >> 15) + 10, ((static_cast<int>(position.y) + 0x40000) >> 15) + 10
    );
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] failed to unride.  no zone(%d, %d, %d)",
        //     L"user_summon.cpp",
        //     0x878,
        //     GetName(),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] failed to unride.  no zone(%d, %d, %d)",
        //     L"user_summon.cpp",
        //     0x878,
        //     GetName(),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot dismount at (%.17g, %.17g, %.17g): geodata zone is missing.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), position.x, position.y, position.z)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot dismount at (%.17g, %.17g, %.17g): geodata zone is missing.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), position.x, position.y, position.z
        );
        return false;
    }

    if (zone->IsNoFlyArea(position)) {
        restart = true;
    }

    position.z += 100.0;
    const int ground =
        geo->FindGround(static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z));
    if (!force && !m_pRidingInfo->IsMealEmpty() && static_cast<double>(ground + 400) < position.z) {
        SendSystemMessage(SystemMessageId::CANNOT_DISMOUNT_AT_HIGH_ELEVATION);
        return false;
    }
    if (ground > 10000 || m_AreaMod.m_bIsInTrainingZone) {
        if (!force && !m_pRidingInfo->IsMealEmpty()) {
            SendSystemMessage(SystemMessageId::CANNOT_DISMOUNT_HERE);
            return false;
        }
        restart = true;
    }

    const double radius = static_cast<double>(static_cast<int>(m_pRidingInfo->Radius()));
    const std::array<FVector, 8> offsets{{
        FVector{radius, radius, 0.0},
        FVector{0.0, radius, 0.0},
        FVector{-radius, radius, 0.0},
        FVector{-radius, 0.0, 0.0},
        FVector{-radius, -radius, 0.0},
        FVector{0.0, -radius, 0.0},
        FVector{radius, -radius, 0.0},
        FVector{radius, 0.0, 0.0},
    }};
    const FVector source = m_data->m_Pos;
    for (const FVector& offset : offsets) {
        const FVector destination{source.x + offset.x, source.y + offset.y, source.z + offset.z};
        const double dx = destination.x - source.x;
        const double dy = destination.y - source.y;
        const double dz = destination.z - source.z;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const bool floating = m_data->IsFlying() || m_AreaMod.m_bIsUnderWater;
        if (!geo->MoveStraightTest(source, destination, distance, floating, m_AreaMod.m_nWaterSurfaceHeight)) {
            SendSystemMessage(SystemMessageId::CANNOT_DISMOUNT_HERE);
            return false;
        }

        FVector collision{};
        if (g_WorldPlaneCollision.CheckCollision(
                source, destination, radius, collision, static_cast<int>(GetInZoneID()), true
            )) {
            SendSystemMessage(SystemMessageId::CANNOT_DISMOUNT_HERE);
            return false;
        }
    }

    m_data->SetPos(position);
    return true;
}

void User::DeleteYongmaAbnormalStatus(bool unknown) {
    L2_GUARD_FUNCTION();
    (void)unknown;

    {
        L2_GUARD(m_InfectedSkillsSect);
        for (;;) {
            int unusableIndex = -1;
            for (std::size_t index = 0; index < m_InfectedSkills.size(); ++index) {
                CSkillInfo* const skillInfo = runtime::CheckedVectorAt(m_InfectedSkills, index).pSkillInfo;
                if (!skillInfo->CanUsedBy(this, true)) {
                    unusableIndex = static_cast<int>(index);
                    break;
                }
            }
            if (unusableIndex < 0) {
                break;
            }
            CSkillInfo* const skillInfo =
                runtime::CheckedVectorAt(m_InfectedSkills, static_cast<std::size_t>(unusableIndex)).pSkillInfo;
            _DeleteAbnormalStatus(unusableIndex, this, skillInfo, true, SPET_RIDING_WYVERN, true);
        }
    }
    CheckAbnormalVisualEffect();
}

bool User::CanDismountOnGround(bool force, bool& restart) {
    L2_GUARD_FUNCTION();

    restart = false;
    if (!force) {
        if (m_pRidingInfo->IsHungry() && !m_pRidingInfo->IsMealEmpty()) {
            return false;
        }
        if (GetController()->GetCurrentActionType() == ACT_SKILL) {
            return false;
        }
    }

    if (!m_pRidingInfo->IsEventRiding()) {
        {
            L2_READ_GUARD(m_inv);
            CItem* const collar = m_inv.GetByDBID(m_pRidingInfo->DBID());
            if (collar == nullptr || !collar->IsEtcItem()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] user[%s] failed to unride.  no collar[%d]",
                //     L"user_summon.cpp",
                //     0x8db,
                //     GetName(),
                //     m_pRidingInfo->DBID())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] user[%s] failed to unride.  no collar[%d]",
                //     L"user_summon.cpp",
                //     0x8db,
                //     GetName(),
                //     m_pRidingInfo->DBID())
                // L2_LOG_ERROR(Log,
                //     "Character %u ('%s'): cannot dismount: collar item %u is missing or is not an etc item.",
                //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
                //     static_cast<unsigned>(m_pRidingInfo->DBID()))
                L2_LOG_ERROR(
                    Log, "Character %u ('%s'): cannot dismount: collar item %u is missing or is not an etc item.",
                    static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                    static_cast<unsigned>(m_pRidingInfo->DBID())
                );
                return false;
            }
            collar->CastEtcItem()->m_bPetCollarTradable = true;
        }
    }
    return true;
}

void User::DismountFromPet() {
    L2_GUARD_FUNCTION();

    DeleteYongmaAbnormalStatus(true);
    if (!m_pRidingInfo->IsEventRiding()) {
        GetSocket()->Send("cdddd", 0x6b, GetServerId(), 3, 0, 0);
    }

    if (m_data->m_rideType == RIDE_WYVERN) {
        m_data->m_moveType = CMT_WALK;
    }
    m_data->m_rideLevel = 0;
    m_data->SetRideType(RIDE_NONE);
    m_data->SetRideClassID(0);
    m_data->SetCollisionRadius(g_PCParamDB.GetCollisionRadius(m_data->m_Sex, m_nBaseClass, m_data->m_Race));
    m_data->SetCollisionHeight(g_PCParamDB.GetCollisionHeight(m_data->m_Sex, m_nBaseClass, m_data->m_Race));
    runtime::raw_atomic::ExchangeI32(&m_initRidingInfo, 1);
    m_data->SetEnvironment(m_AreaMod.m_bIsUnderWater ? ENV_UNDERWATER : ENV_GROUND);

    const FVector position = GetPos();
    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborRect(
            this, 0x80000, position, 0x800, "cddddddd", 0x8c, static_cast<int>(Id()), 0, 1, 0, static_cast<int>(position.x),
            static_cast<int>(position.y), static_cast<int>(position.z)
        );
    } else {
        GetSocket()->Send(
            "cddddddd", 0x8c, static_cast<int>(Id()), 0, 1, 0, static_cast<int>(position.x), static_cast<int>(position.y),
            static_cast<int>(position.z)
        );
    }
    ValidateChanged(VB_MAX);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PET});
    m_pRidingInfo->MakeLog(0x74, this);
}

bool User::Dismount(bool force, bool broadcast) {
    L2_GUARD_FUNCTION();

    if (!m_pRidingInfo->IsEventRiding() && !m_SkillMod.m_bCanAct) {
        return false;
    }

    bool restart = false;
    if (!IsRidingOnGround()) {
        if (!CanDismountOnAir(force, restart)) {
            return false;
        }
        DeleteAcquireSkill(0x10c1);
        restart = restart || m_pRidingInfo->IsMealEmpty() || (force && m_pRidingInfo->IsEventRiding());
    } else if (!CanDismountOnGround(force, restart)) {
        return false;
    }

    DismountFromPet();
    if (broadcast && restart) {
        Restart(RESTART_VILLAGE, 0);
    } else {
        const FVector position = GetPos();
        const int ground = CWorld::Inst().GetGeo()->FindGround(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
        );
        MoveToLocation(static_cast<int>(position.x) + 5, static_cast<int>(position.y) + 5, ground, CCreature::UsePathFind);
    }
    return true;
}

bool User::CheckWyvernOnTimer() {
    // Current L2Server 0x00bb57f8.
    L2_GUARD_FUNCTION();

    if (m_data->m_bIsAlive == 0 || !IsRidingWyvern()) {
        return false;
    }
    if (m_NoLimitFlying) {
        return true;
    }

    if (m_pRidingInfo->IsEventRiding()) {
        const int timeLeft = m_pRidingInfo->TimeLeft();
        if (timeLeft <= 0) {
            static_cast<void>(Dismount(true, true));
            return false;
        }
        if (timeLeft == 60) {
            SendSystemMessage_d(SystemMessageId::WYVERN_RIDING_MINUTES_REMAINING, 1);
        } else if (timeLeft == 5 || timeLeft == 10) {
            SendSystemMessage_d(SystemMessageId::WYVERN_RIDING_SECONDS_REMAINING, timeLeft);
        }
    }

    const FVector position = GetPos();
    CWorld& world = CWorld::Inst();
    const std::int32_t positionY = runtime::Win64CvttDoubleToInt32(position.y);
    const std::int32_t positionX = runtime::Win64CvttDoubleToInt32(position.x);
    CGeoData* const geo = world.GetGeo();
    const std::int32_t adjustedY = runtime::Int32FromBits(static_cast<std::uint32_t>(positionY) + 0x40000U);
    const std::int32_t adjustedX = runtime::Int32FromBits(static_cast<std::uint32_t>(positionX) + 0x50000U);
    CGeoZone* const zone = geo->GetZone((adjustedX >> 15) + 10, (adjustedY >> 15) + 10);
    if (zone == nullptr) {
        return false;
    }

    if (!zone->IsNoFlyArea(position) && !m_AreaMod.m_bIsInTrainingZone) {
        m_nNoFlyingAreaCount = 0;
    } else {
        GetSocket()->SendSystemMessage(SystemMessageId::LEAVE_RESTRICTED_AREA_OR_BE_DISMOUNTED_FROM_WYVERN);
        m_nNoFlyingAreaCount = runtime::Int32FromBits(static_cast<std::uint32_t>(m_nNoFlyingAreaCount) + 1U);
    }
    if (m_nNoFlyingAreaCount > 20) {
        static_cast<void>(Dismount(true, true));
    }

    if (m_AreaMod.m_bIsInBattleField) {
        CWorld& battlefieldWorld = CWorld::Inst();
        const FVector zoneYPosition = GetPos();
        const std::int32_t battlefieldPositionY = runtime::Win64CvttDoubleToInt32(zoneYPosition.y);
        const FVector zoneXPosition = GetPos();
        const std::int32_t battlefieldPositionX = runtime::Win64CvttDoubleToInt32(zoneXPosition.x);
        const std::int32_t battlefieldAdjustedY =
            runtime::Int32FromBits(static_cast<std::uint32_t>(battlefieldPositionY) + 0x40000U);
        const std::int32_t battlefieldAdjustedX =
            runtime::Int32FromBits(static_cast<std::uint32_t>(battlefieldPositionX) + 0x50000U);
        CGeoZone* const battlefieldZone =
            battlefieldWorld.GetGeo()->GetZone((battlefieldAdjustedX >> 15) + 10, (battlefieldAdjustedY >> 15) + 10);

        CGeoSector* sector = nullptr;
        if (battlefieldZone != nullptr) {
            const FVector sectorYPosition = GetPos();
            const std::int32_t sectorPositionY = runtime::Win64CvttDoubleToInt32(sectorYPosition.y);
            const FVector sectorXPosition = GetPos();
            const std::int32_t sectorPositionX = runtime::Win64CvttDoubleToInt32(sectorXPosition.x);
            sector = battlefieldZone->GetSector((sectorPositionX >> 7) & 0xff, (sectorPositionY >> 7) & 0xff);
        }

        const std::uint16_t battleResidenceMask = GeoFieldMask[GEO_FIELD_BATTLE_RESIDENCE];
        if (sector != nullptr &&
            (static_cast<std::uint16_t>(sector->m_Field) & battleResidenceMask) == battleResidenceMask) {
            CResidence* const residence = CResidence::GetBattleFieldResidence(GetPos());
            bool restartAtVillage = false;
            if (residence != nullptr) {
                if (residence->IsCastle()) {
                    CCastle* const castle = residence->CastCastle();
                    if (IsInRealWorld() && castle != nullptr && castle->IsCastle() &&
                        castle->IsInResidence(GetPos(), false) && castle->GetResidenceOwnerPledge() != nullptr &&
                        castle->GetResidenceOwnerPledge()->GetDbId() != m_data->m_nPledgeID) {
                        restartAtVillage = true;
                    }
                } else if (IsInRealWorld() && residence->IsInResidence(GetPos(), false)) {
                    restartAtVillage = true;
                }
            }
            if (restartAtVillage) {
                SendSystemMessage(SystemMessageId::LEAVE_RESTRICTED_AREA_OR_BE_DISMOUNTED_FROM_WYVERN);
                static_cast<void>(Dismount(true, true));
                static_cast<void>(Restart(RESTART_VILLAGE, 0));
            }
        }
    }

    if (IsInAgitWar(true) && IsInRealWorld()) {
        SendSystemMessage(SystemMessageId::LEAVE_RESTRICTED_AREA_OR_BE_DISMOUNTED_FROM_WYVERN);
        static_cast<void>(Dismount(true, false));
        static_cast<void>(Restart(RESTART_VILLAGE_BY_DISMOUNT, 0));
    }
    return true;
}

bool User::TryDismounting(bool force, bool broadcast) {
    L2_GUARD_FUNCTION();
    return IsRiding() && Dismount(force, broadcast);
}

bool User::OnPetStep(int step) {
    // Current L2Server 0x00bb25f8.
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnCommand) == PETSPAWNCMD_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]- %s m_nPetSummonCommand = PETSUMMONCMD_NONE",
        //     "user_summon.cpp",
        //     0x206,
        //     "User::OnPetStep")
        // L2_LOG_ERROR(Log,
        //     "[%s][%d]- %s m_nPetSummonCommand = PETSUMMONCMD_NONE",
        //     "user_summon.cpp",
        //     0x206,
        //     "User::OnPetStep")
        // L2_LOG_WARN(Log,
        //     "Character %u ('%s'): pet step %d arrived with no active spawn command. Step rejected.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), step)
        L2_LOG_WARN(
            Log, "Character %u ('%s'): pet step %d arrived with no active spawn command; step rejected.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), step
        );
        return false;
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnCommand) == PETSPAWNCMD_DESPAWN) {
        if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnStep) != PETSPAWNSTEP_DESPAWN ||
            static_cast<std::uint32_t>(step) - PETSPAWNSTEP_DESPAWN_BASE >=
                PETSPAWNSTEP_DESPAWN_MAX - PETSPAWNSTEP_DESPAWN_BASE) {
            return false;
        }
        if (runtime::raw_atomic::IncrementI32(&m_nPetDespawnSteps) == 4) {
            EndDespawnPet();
        }
        return true;
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnCommand) != PETSPAWNCMD_SPAWN) {
        return false;
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnStep) == PETSPAWNSTEP_DESPAWN &&
        static_cast<std::uint32_t>(step) - PETSPAWNSTEP_DESPAWN_BASE <
            PETSPAWNSTEP_DESPAWN_MAX - PETSPAWNSTEP_DESPAWN_BASE) {
        const std::int32_t completedSteps = runtime::raw_atomic::IncrementI32(&m_nPetDespawnSteps);
        CSPointer<CItem> petCollar(ItemSmartIdManager()->GetObjectW(m_nPetCollarSID), ObjectRefType::FindObject);
        if (!petCollar) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]- %s m_nPetSummonCommand = PETSUMMONCMD_NONE",
            //     "user_summon.cpp",
            //     0x223,
            //     "User::OnPetStep")
            // L2_LOG_ERROR(Log,
            //     "[%s][%d]- %s m_nPetSummonCommand = PETSUMMONCMD_NONE",
            //     "user_summon.cpp",
            //     0x223,
            //     "User::OnPetStep")
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): pet despawn step %d cannot find collar object %u after %d completed steps. Clearing
            //     spawn state.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), step,
            //     static_cast<unsigned>(m_nPetCollarSID), completedSteps)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): pet despawn step %d cannot find collar object %u after %d completed steps; clearing "
                "spawn state.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), step,
                static_cast<unsigned>(m_nPetCollarSID), completedSteps
            );
            ClearPetSpawn();
            return true;
        }

        if (completedSteps == 4) {
            runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, PETSPAWNSTEP_WITHDRAWPET_START);
            db.WithdrawPet(this, petCollar.get(), m_bNewlySummon);
        }
        return true;
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnStep) == PETSPAWNSTEP_WITHDRAWPET_START &&
        step == PETSPAWNSTEP_WITHDRAWPETITEMS_START) {
        runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, PETSPAWNSTEP_WITHDRAWPETITEMS_START);
        return true;
    }
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nPetSpawnStep) == PETSPAWNSTEP_WITHDRAWPETITEMS_START &&
        step == PETSPAWNSTEP_WITHDRAWPETITEMS_COMPLETE) {
        runtime::raw_atomic::ExchangeI32(&m_nPetSpawnStep, PETSPAWNSTEP_WITHDRAWPETITEMS_COMPLETE);
        EndSpawnPet();
        return true;
    }
    return false;
}

void User::OnReplyEvolvePet(const unsigned char* packet) {
    // Current L2Server 0x00bb2994.
    L2_GUARD_FUNCTION();

    std::uint8_t evolvedPetCreated = 0;
    std::int32_t oldCollarDatabaseId = 0;
    std::int32_t newCollarClassId = 0;
    std::int32_t newCollarDatabaseId = 0;
    std::int32_t newPetLevel = 0;
    packet = ::Disassemble(
        packet, "cdddd", &evolvedPetCreated, &oldCollarDatabaseId, &newCollarClassId, &newCollarDatabaseId, &newPetLevel
    );

    std::int32_t oldCollarDamage;
    {
        L2_WRITE_GUARD(m_inv);
        CItem* const oldCollar = m_inv.GetByDBID(oldCollarDatabaseId);
        if (oldCollar == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ReplyEvolvePet failed. no such item, DBID[%d]",
            //     L"user_summon.cpp",
            //     0x3e4,
            //     oldCollarDatabaseId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] ReplyEvolvePet failed. no such item, DBID[%d]",
            //     L"user_summon.cpp",
            //     0x3e4,
            //     oldCollarDatabaseId)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): cannot apply pet evolution: old collar item %u is missing.",
            //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
            //     static_cast<unsigned>(oldCollarDatabaseId))
            L2_LOG_ERROR(
                Log, "Character %u ('%s'): cannot apply pet evolution: old collar item %u is missing.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(oldCollarDatabaseId)
            );
            return;
        }

        oldCollarDamage = oldCollar->m_data->m_nDamaged;
        const std::int32_t oldCollarIndex = oldCollar->m_data->index;
        m_inv.Pop(oldCollarIndex, false);
        m_inv.SetInventoryChanged(oldCollar, IUT_DELETE);
        oldCollar->Delete();
    }

    CItem* const newCollar = g_ObjectDB.CreateItem(newCollarClassId);
    if (newCollar == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] ReplyEvolvePet Failed. item class id[%d]",
        //     L"user_summon.cpp",
        //     0x3f9,
        //     newCollarClassId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] ReplyEvolvePet Failed. item class id[%d]",
        //     L"user_summon.cpp",
        //     0x3f9,
        //     newCollarClassId)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): pet evolution cannot create new collar class %d after old collar %u deletion was
        //     requested.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), newCollarClassId,
        //     static_cast<unsigned>(oldCollarDatabaseId))
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): pet evolution cannot create new collar class %d after old collar %u deletion was "
            "requested.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), newCollarClassId,
            static_cast<unsigned>(oldCollarDatabaseId)
        );
        return;
    }
    if (!newCollar->SetAmount(1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item amount is invalid[%d][%d]",
        //     "user_summon.cpp",
        //     0x400,
        //     newCollar->m_data->m_nCount,
        //     1)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] item amount is invalid[%d][%d]",
        //     "user_summon.cpp",
        //     0x400,
        //     newCollar->m_data->m_nCount,
        //     1)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): pet evolution could not set collar class %d count to 1 (current %lld). Processing
        //     continues.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), newCollarClassId,
        //     static_cast<long long>(newCollar->m_data->m_nCount))
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): pet evolution could not set collar class %d count to 1 (current %lld); processing "
            "continues.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), newCollarClassId,
            static_cast<long long>(newCollar->m_data->m_nCount)
        );
    }
    newCollar->SetDBID(static_cast<unsigned int>(newCollarDatabaseId));
    newCollar->SetEnchanted(newPetLevel);
    newCollar->m_data->m_nDamaged = oldCollarDamage;

    {
        L2_WRITE_GUARD(m_inv);
        if (!m_inv.Push(newCollar->m_data->index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "Inventory push failed at file[%s], line[%d]",
            //     "user_summon.cpp",
            //     0x40d)
            // L2_LOG_ERROR(Log,
            //     "Inventory push failed at file[%s], line[%d]",
            //     "user_summon.cpp",
            //     0x40d)
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): pet evolution cannot insert new collar %u (class %d) into inventory. Old collar
            //     deletion was already requested.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
            //     static_cast<unsigned>(newCollarDatabaseId), newCollarClassId)
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): pet evolution cannot insert new collar %u (class %d) into inventory; old collar "
                "deletion was already requested.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), static_cast<unsigned>(newCollarDatabaseId),
                newCollarClassId
            );
            return;
        }
        m_inv.SetInventoryChanged(newCollar, IUT_ADD);
    }

    if (evolvedPetCreated == 1) {
        CPet* const oldPet = GetPet();
        if (oldPet != nullptr) {
            std::int32_t newPetNpcClassId = 0;
            std::int32_t meal = 0;
            std::int64_t sp = 0;
            std::int64_t experience = 0;
            ::Disassemble(packet, "ddQQ", &newPetNpcClassId, &meal, &sp, &experience);

            const FVector position = oldPet->GetPos();
            CPet* newPet;
            {
                L2_GUARD(*m_lock);
                CNPC* const npc = g_ObjectDB.CreateNPC(newPetNpcClassId, true);
                if (npc == nullptr || !npc->IsPet()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "There is no NPC class [%d] at file[%s], line[%d]",
                    //     newPetNpcClassId,
                    //     "user_summon.cpp",
                    //     0x428)
                    // L2_LOG_ERROR(Log,
                    //     "There is no NPC class [%d] at file[%s], line[%d]",
                    //     newPetNpcClassId,
                    //     "user_summon.cpp",
                    //     0x428)
                    // L2_LOG_ERROR(Log,
                    //     "Character %u ('%s'): pet evolution cannot create pet NPC class %d: template is missing or has
                    //     another type, new collar is already in inventory.", static_cast<unsigned>(GetDBID()),
                    //     WideToUtf8(GetName()).c_str(), newPetNpcClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Character %u ('%s'): pet evolution cannot create pet NPC class %d: template is missing or has "
                        "another type; new collar is already in inventory.",
                        static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), newPetNpcClassId
                    );
                    return;
                }

                newPet = npc->CastPet();
                newPet->SetNickName(oldPet->m_sNickName);
                g_PetManager.DeletePet(GetDBID(), Id());
                m_PetSID = 0;
                newPet->SetDBID(newCollar->GetDBID());
                g_PetManager.LinkPet(this, newPet);
            }

            newPet->m_data->SetSP(sp);
            newPet->m_data->SetHP(newPet->m_data->m_dBaseHP);
            newPet->m_data->SetMP(newPet->m_data->m_dBaseMP);
            newPet->m_data->m_Meal = meal;
            newPet->m_data->SetPos(position);
            newPet->m_data->m_nPetDBID = static_cast<std::int32_t>(newCollar->GetDBID());
            newPet->SetMasterName(GetName());
            if (newPet->m_data->m_nLevel != newPetLevel) {
                newPet->m_data->SetLevel(newPetLevel);
            }
            if (!newPet->IsSyncLevel()) {
                static_cast<void>(newPet->SetExp(experience));
            }
            newPet->ValidateChanged(VB_MAX);
            newPet->SetPrivateID(GetPrivateID());
            db.LoadPetItems(newPet, *this, true, 0);
        }
    }

    SendItemList(true, true, nullptr);
}

void User::PetLevelChanged(const int petDatabaseId, const int level) {
    // Current L2Server 0x00bb140c.
    L2_GUARD_FUNCTION();
    CItem* const petCollar = m_inv.GetByDBID(petDatabaseId);
    if (petCollar == nullptr) {
        return;
    }

    petCollar->SetEnchanted(level);
    {
        L2_WRITE_GUARD(m_inv);
        m_inv.SetInventoryChanged(petCollar, IUT_UPDATE);
    }
    SendItemList(true, true, nullptr);
}

void User::PetNameChanged(const int petDatabaseId) {
    // Current L2Server 0x00bb152c.
    L2_GUARD_FUNCTION();
    CItem* const petCollar = m_inv.GetByDBID(petDatabaseId);
    if (petCollar == nullptr) {
        return;
    }

    petCollar->m_data->m_nDamaged = 1;
    {
        L2_WRITE_GUARD(m_inv);
        m_inv.SetInventoryChanged(petCollar, IUT_UPDATE);
    }
    SendItemList(true, true, nullptr);
}

void User::OnDespawnPet() {
    L2_GUARD_FUNCTION();

    switch (m_jobOnDespawnPet) {
        case DESPAWN_PET_OLYMPIAD_PLAYER:
            m_pWOlympiadUser->OnDespawnPetByPlayer();
            break;
        case DESPAWN_PET_OLYMPIAD_OBSERVER:
            m_pWOlympiadUser->OnDespawnPetByObserver();
            break;
        case DESPAWN_PET_PLEDGEGAME_PLAYER:
            m_PledgeGameUser.CompleteToDespawnPet(false);
            break;
        case DESPAWN_PET_PLEDGEGAME_OBSERVER:
            m_PledgeGameUser.CompleteToDespawnPet(true);
            break;
        case DESPAWN_PET_SUBJOB_CHANGE:
            OnChangeSubJobBegin();
            db.RequestChangeSubJob(this, m_nJobOnDespawnPetCompleteArgument);
            break;
        case DESPAWN_PET_OLYMPIAD_IN_PEER:
            m_pWOlympiadUser->OnDespawnPetInPeer();
            break;
        default:
            break;
    }
    m_jobOnDespawnPet = DESPAWN_PET_NONE;
}

void User::DespawnPetByOlympiadInPeer() {
    L2_GUARD_FUNCTION();

    CPet* const pet = GetPet();
    if (pet == nullptr) {
        m_jobOnDespawnPet = DESPAWN_PET_OLYMPIAD_IN_PEER;
        OnDespawnPet();
        return;
    }

    if (!BeginDespawnPet()) {
        return;
    }
    m_jobOnDespawnPet = DESPAWN_PET_OLYMPIAD_IN_PEER;
    db.RequestDeleteSummon(*this, static_cast<int>(pet->GetDBID()), g_AtomicPetDBJob);
    pet->DeleteInfectedSkills(g_AtomicPetDBJob);
    db.DepositPet(this, pet, false, g_AtomicPetDBJob);
}

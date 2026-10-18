#include "Alliance.h"
#include "Area.h"
#include "Artifact.h"
#include "CConfig.h"
#include "CWorld.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Item.h"
#include "ItemOptionServer.h"
#include "NPC.h"
#include "NpcServer.h"
#include "OlympiadStuff.h"
#include "PVPMatch.h"
#include "Pet.h"
#include "Pledge.h"
#include "Portable/ClientProtocolRuntime.h"
#include "PotentialSkill.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Relay.h"
#include "SerializedMessage.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "SkillEnchant.h"
#include "Transform.h"
#include "User.h"
#include "UserBasicAction.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>

RecallInfo::RecallInfo() :
    m_nRecallerSID(0), m_nRecalledTime(0), m_nItemClassID(0), m_nItemCount(0), m_bIncludeDead(false) {}

namespace {


#if !defined(_WIN32)
const wchar_t* UserSkillAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& destination) noexcept {
    destination.fill(L'\0');
    std::size_t output = 0;
    for (const char16_t value : source) {
        if (value == u'\0' || output + 1 >= destination.size()) {
            break;
        }
        destination[output++] = static_cast<wchar_t>(value);
    }
    return destination.data();
}
#endif

// Copy at most 23 UTF-16 units, as the original name buffer did.
void CopyUserSkillName(std::array<char16_t, 28>& destination, const char16_t* source) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return;
    }
    std::size_t index = 0;
    while (index < 23U && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
}

void LeaveOnlyLearnableSkills(User& user, std::map<CSkillKey, User::AcquireInfo>& acquireList) {
    // Current L2Server 0x00baa414.
    L2_GUARD_FUNCTION();
    auto position = acquireList.begin();
    while (position != acquireList.end()) {
        if (!user.IsLearnableSkill(*position->second.m_pAcquireInfo, false)) {
            position = acquireList.erase(position);
        } else {
            ++position;
        }
    }
}

} // namespace

void User::LogWhenPCDie(CCreature* killer, FVector& position [[maybe_unused]]) {
    // Current L2Server 0x00ba7dcc, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (killer == nullptr) {
        return;
    }

    CLogFormatter formatter;
    if (killer->IsPC()) {
        User* const killerUser = killer->CastUser();
        if (killerUser != nullptr) {
            formatter.WriteLog2_1(
                0x457, killerUser, this, m_data->m_nLevel, GetVitalPoint(), m_data->m_nPKCount,
                killerUser->m_data->m_nPKCount, m_data->m_Race, 0, 0
            );
            const int relation = GetRelationTo(killer->CastUser());
            RelayClient::Inst()->SendPcKillPacket(this, killerUser, ((relation >> 15) & 1) == 0 ? 1 : 0);
        }
    } else if (killer->IsNPC()) {
        CNPC* const npc = killer->CastNPC();
        if (npc != nullptr) {
            formatter.WriteLog4(
                0x451, npc, this, nullptr, nullptr, nullptr, m_data->m_nLevel, GetVitalPoint(), m_data->m_nPKCount,
                npc->m_data->m_nClass, 0, 0, 0, 0, 0, 0, 0
            );

            User* const master = npc->GetMaster();
            if (master != nullptr) {
                std::array<char16_t, 28> accountName{};
                CopyUserSkillName(accountName, master->m_sAccountName.data());
                // formatter.WriteLog4(
                //     0x45c,
                //     npc,
                //     this,
                //     master->GetName(),
                //     accountName.data(),
                //     nullptr,
                //     m_data->m_Race,
                //     m_data->m_Sex,
                //     m_data->m_nClass,
                //     m_data->m_nLevel,
                //     master->GetDBID(),
                //     master->GetAccountID(),
                //     npc->m_data->m_SummonType,
                //     0,
                //     0,
                //     0,
                //     0)
                formatter.WriteLog4(
                    0x45c, npc, this, master->GetName(), accountName.data(), nullptr, m_data->m_Race, m_data->m_Sex,
                    m_data->m_nClass, m_data->m_nLevel, master->GetDBID(), master->GetAccountID(), npc->m_data->m_SummonType,
                    0, 0, 0, 0
                );
            }
        }
    }

    CCreature* normalizedKiller = killer;
    if (killer->IsSummonOrPet()) {
        CSummon* const summon = killer->CastSummon();
        if (summon != nullptr && summon->GetMaster() != nullptr) {
            normalizedKiller = summon->GetMaster();
        }
    }
    User* const killerUser = killer->GetUserOrMaster();
    if (killerUser == nullptr) {
        return;
    }

    std::array<char16_t, 28> killerPledgeName{};
    std::array<char16_t, 28> killerAllianceName{};
    std::array<char16_t, 28> victimPledgeName{};
    std::uint32_t killerPledgeId = 0;
    std::uint32_t killerAllianceId = 0;
    std::uint32_t victimDbId = 0;

    CPledge* const killerPledge = killerUser->GetPledge();
    if (killerPledge != nullptr) {
        CopyUserSkillName(killerPledgeName, killerPledge->GetPledgeName());
        killerPledgeId = killerPledge->GetDbId();
        CAlliance* const alliance = killerPledge->GetAlliance();
        if (alliance != nullptr && alliance->m_data != nullptr) {
            CopyUserSkillName(killerAllianceName, alliance->m_data->m_sAllianceName);
            killerAllianceId = alliance->m_data->m_nDbId;
        }
    }

    if (CPledge* const victimPledge = GetPledge()) {
        CopyUserSkillName(victimPledgeName, victimPledge->GetPledgeName());
        victimDbId = GetDBID();
    }

    const bool warBoth = CheckDeclareWarBoth(killerUser);
    if (!warBoth && IsMakeAttackerChao(normalizedKiller)) {
        formatter.WriteLog1_1(
            0x45a, killerUser, this, killerPledgeName.data(), killerAllianceName.data(), victimPledgeName.data(),
            killerUser->m_data->m_nPKCount, killerUser->m_data->m_nKarma, killerPledgeId, killerAllianceId, victimDbId, 0, 0
        );
    } else {
        formatter.WriteLog1_1(
            0x459, killerUser, this, killerPledgeName.data(), killerAllianceName.data(), victimPledgeName.data(),
            killerUser->m_data->m_nDuelCount, killerPledgeId, killerAllianceId, victimDbId,
            killerUser->CheckPremiumType(PremiumUserType::PUT_FREE_USER) ? 0 : 1, 0, 0
        );
    }
}

int User::SaveInfectedSkills() {
    // Current L2Server 0x00baf36c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (!m_bAbnormalStatusLoaded) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Trying save skills before loading user[%s]",
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     L"Trying save skills before loading user[%s]",
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot save abnormal effects before they have loaded. Save skipped.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str())
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot save abnormal effects before they have loaded; save skipped.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str()
        );
        return 0;
    }

    std::vector<InfectedSkillType> infectedSkills;
    GetInfectedSkillsToDBSave(infectedSkills);
    if (infectedSkills.size() > 0x60) {
        L2_GUARD(m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : m_InfectedSkills) {
            const CSkillInfo* const skillInfo = infected.pSkillInfo;
            // Log.Add(
            //     LOG_ERROR,
            //     L"SaveInfectedSkills skill_id[%d][%d] UserID[%d] "
            //     L"UserName[%s] CasterId[%d] Size[%d]",
            //     skillInfo->m_nSkillId,
            //     skillInfo->m_nLevel,
            //     GetDBID(),
            //     GetName(),
            //     infected.nSourceCreatureId,
            //     static_cast<int>(infectedSkills.size()))
            // L2_LOG_ERROR(Log,
            //     L"SaveInfectedSkills skill_id[%d][%d] UserID[%d] "
            //     L"UserName[%s] CasterId[%d] Size[%d]",
            //     skillInfo->m_nSkillId,
            //     skillInfo->m_nLevel,
            //     GetDBID(),
            //     GetName(),
            //     infected.nSourceCreatureId,
            //     static_cast<int>(infectedSkills.size()))
            // L2_LOG_ERROR(Log,
            //     "Character %u ('%s'): abnormal-effect save has %zu entries above diagnostic threshold 96. Current effect
            //     skill %d level %d, caster object %u, skill info %s. Saving continues.", static_cast<unsigned>(GetDBID()),
            //     WideToUtf8(GetName()).c_str(), infectedSkills.size(), skillInfo ? skillInfo->m_nSkillId : 0, skillInfo ?
            //     static_cast<int>(skillInfo->m_nLevel) : 0, static_cast<unsigned>(infected.nSourceCreatureId), skillInfo ?
            //     "present" : "missing")
            L2_LOG_ERROR(
                Log,
                "Character %u ('%s'): abnormal-effect save has %zu entries above diagnostic threshold 96; current effect "
                "skill %d level %d, caster object %u, skill info %s. Saving continues.",
                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), infectedSkills.size(),
                skillInfo ? skillInfo->m_nSkillId : 0, skillInfo ? static_cast<int>(skillInfo->m_nLevel) : 0,
                static_cast<unsigned>(infected.nSourceCreatureId), skillInfo ? "present" : "missing"
            );
        }
    }

    CPacketHelperEx<20480> packet;
    PackingInfectedSkillsToDBSave(infectedSkills, packet);
    const std::int32_t now = runtime::crt::Time32Now();
    for (const InfectedSkillType& infected : infectedSkills) {
        const CSkillInfo* const skillInfo = infected.pSkillInfo;
        if (skillInfo == nullptr || skillInfo->m_nSpecialLevel >= 0 || skillInfo->m_nSpecialLevel <= -5 ||
            !skillInfo->m_IrreplaceableBuff) {
            continue;
        }

        const std::uint32_t skillLevelBits =
            (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16) | static_cast<std::uint16_t>(skillInfo->m_nLevel);
        const std::int32_t remainingTime = runtime::AddInt32Wrapping(
            GetAbnormalTime(skillInfo, true), runtime::SubtractInt32Wrapping(infected.nStartTime, now)
        );
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1ae, this, nullptr, skillInfo->m_nSkillId, runtime::Int32FromBits(skillLevelBits), remainingTime,
            skillInfo->m_nAbnormalTime, m_SkillDurationMod.m_RechargeVitalityPoint, 0, 0
        );
    }

    const int skillCount = static_cast<int>(infectedSkills.size());
    db.SaveInfectedSkills(*this, skillCount, packet.GetLength(), packet.GetPacket());

    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            this, summon,
            // Recovered source used: if (summon != nullptr) { ... }
            // Current 0x00baf36c pushes "in_code" before the save call.
            summon->SaveInfectedSkills(nullptr);
        );
    }
    return skillCount;
}

bool IsInNoCallPCArea(const FVector& position) {
    // Current L2Server 0x00ba71a0, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    CGeoData* const geoData = CWorld::Inst().GetGeo();
    if (geoData == nullptr) {
        return false;
    }

    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    CGeoZone* const zone = geoData->GetZone(((x + 0x50000) >> 15) + 10, ((y + 0x40000) >> 15) + 10);
    return zone != nullptr && zone->IsInNoCallPCArea(position);
}

bool IsCallPCEnableArea(User* affected, User* receiver, const FVector& position, User::CallPCResult& result) {
    // Current L2Server 0x00ba872c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (affected->IsOlympiadPlaying()) {
        receiver->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_OR_TELEPORT_OLYMPIAD_PARTICIPANT);
        result = User::CPR_ETC;
        return false;
    }
    if (affected->m_PledgeGameUser.IsPlaying()) {
        receiver->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_DURING_COMBAT);
        result = User::CPR_ETC;
        return false;
    }

    CAreaDB& areaDb = CAreaDB::inst();
    CArea* const noRestartArea =
        areaDb.FindArea(static_cast<int>(affected->GetInZoneID()), position, AREAT_NO_RESTART, true);
    if (affected->IsInVehicle() || affected->IsInAirShip() || affected->IsInShuttle()) {
        result = User::CPR_UNENABLEAREA;
        return false;
    }

    const bool inSevenSignsArea = affected->m_AreaMod.m_bIsInSSQZone &&
        areaDb.FindArea(static_cast<int>(affected->GetInZoneID()), position, AREAT_SSQ, true) != nullptr;
    if ((affected->m_AreaMod.m_bIsInPvPZone || !affected->m_AreaMod.m_bIsInBattleField) && !affected->IsInAgitWar(false) &&
        noRestartArea == nullptr && !inSevenSignsArea && !IsInNoCallPCArea(position) && affected->IsInRealWorld() &&
        !affected->IsInPVPMatch(PVPMatchType::NONE, PVPMatchPlayerState::NONE)) {
        return true;
    }

    CUserSocket* const socket = receiver->GetSocket();
    if (affected == receiver) {
        socket->SendSystemMessage(SystemMessageId::CURRENT_LOCATION_BLOCKS_SUMMON_OR_TELEPORT);
    } else {
        socket->SendSystemMessage_S(SystemMessageId::TARGET_LOCATION_BLOCKS_SUMMON_OR_TELEPORT, affected->GetName());
    }
    result = User::CPR_UNENABLEAREA;
    return false;
}

bool User::IsInNoCallArea() {
    // Current L2Server 0x00ba7290, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    return IsInNoCallPCArea(m_data->m_Pos);
}

void User::AcquireMentorMenteeSkills(EtcSkillAcquireType type) {
    // Current L2Server 0x00ba8390, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    const auto& skills = g_SkillAcquireDB.GetAcquirableEtcSkills(type);
    AcquireSkills(skills, false, false);
    ValidateChanged(VB_SKILLLIST);
}

bool User::DeleteAcquireSkills(EtcSkillAcquireType type) {
    // Current L2Server 0x00ba930c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> packet;
    int deletedCount = 0;
    const auto& skills = g_SkillAcquireDB.GetAcquirableEtcSkills(type);
    for (SkillAcquireInfo* const skill : skills) {
        if (!skill->bAutoGet && !skill->m_bExcludedFromListOfSkillsToLearn && DeleteAcquireSkill(skill->nSkillID)) {
            packet.Assemble("d", skill->nSkillID);
            ++deletedCount;
        }
    }

    if (deletedCount <= 0) {
        return false;
    }
    ValidateChanged(VB_MAX);
    ParamChanged(std::int64_t{PCB_USER});
    db.RequestDeleteSkillList(this, deletedCount, &packet);
    return true;
}

void User::DeleteMentorMenteeSkills(EtcSkillAcquireType type) {
    // Current L2Server 0x00ba9934, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(DeleteAcquireSkills(type));
    ValidateChanged(VB_SKILLLIST);
}

bool User::IsCallPCEnable(User* source, User* target, bool includeDead, CallPCResult& result) {
    // Current L2Server 0x00ba89ac, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    result = CPR_ETC;
    if (source == nullptr || target == nullptr) {
        return false;
    }

    CUserSocket* const sourceSocket = source->GetSocket();
    const FVector sourcePosition = source->GetPos();
    const FVector targetPosition = target->GetPos();
    if (target->m_data->m_bIsInCombatMode) {
        sourceSocket->SendSystemMessage_S(SystemMessageId::CANNOT_SUMMON_OR_TELEPORT_CHARACTER_IN_COMBAT, target->GetName());
        return false;
    }
    if (!target->m_data->m_bIsAlive && !includeDead) {
        sourceSocket->SendSystemMessage_S(SystemMessageId::CANNOT_SUMMON_OR_TELEPORT_DEAD_CHARACTER, target->GetName());
        return false;
    }
    if (!target->m_data->m_bIsActive || target->m_bIsObserving || target->IsGMInvisible() || target->IsCameraMode()) {
        sourceSocket->SendSystemMessage_S(SystemMessageId::TARGET_UNAVAILABLE_FOR_SUMMON_OR_TELEPORT, target->GetName());
        return false;
    }
    if (target->IsNowTrade() || target->m_data->m_nPKCount == 2) {
        sourceSocket->SendSystemMessage_S(
            SystemMessageId::CANNOT_SUMMON_OR_TELEPORT_CHARACTER_TRADING_OR_RUNNING_STORE, target->GetName()
        );
        return false;
    }
    if (!IsCallPCEnableArea(source, source, sourcePosition, result) ||
        !IsCallPCEnableArea(target, source, targetPosition, result)) {
        return false;
    }
    if (source->m_DuelUserInfo.IsInPartyDuel() || target->m_DuelUserInfo.IsInPartyDuel()) {
        return false;
    }
    if (source->IsRidingWyvern() || source->IsInAirShip() || static_cast<int>(source->m_data->m_moveType) == CMT_FLY ||
        target->IsRidingWyvern() || target->IsInAirShip() || static_cast<int>(target->m_data->m_moveType) == CMT_FLY) {
        return false;
    }
    if (source->GetInZoneID() != target->GetInZoneID()) {
        return false;
    }
    if (target->HasFlagOfSiege()) {
        target->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
        return false;
    }
    if (target->m_bFlyMoving || target->m_data->m_TrainingState == 1) {
        return false;
    }

    result = CPR_SUCCESS;
    return true;
}

void User::CallPCTry(User* caller, int itemClassId, int itemCount, bool includeDead) {
    // Current L2Server 0x00ba8d00, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_RecallLock);
        // Current 0x00ba8db6..0x00ba8de3 calls the +0xb8
        // GetInZoneID vslot for the caller and recalled user.
        if (caller->GetInZoneID() != 0 && caller->GetInZoneID() != GetInZoneID()) {
            return;
        }

        if (m_RecallInfo.m_nRecallerSID != 0 && runtime::GetTickCount() - m_RecallInfo.m_nRecalledTime < 40000U) {
            caller->GetSocket()->SendSystemMessage_S(SystemMessageId::CHARACTER_ALREADY_BEING_SUMMONED, GetName());
            return;
        }

        if (HasFlagOfSiege()) {
            SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_BLOCKED_WHILE_HOLDING_WARD);
            return;
        }

        m_RecallInfo.m_nRecallerSID = static_cast<std::int32_t>(caller->m_nId);
        m_RecallInfo.m_nRecalledTime = runtime::GetTickCount();
        m_RecallInfo.m_nItemClassID = itemClassId;
        m_RecallInfo.m_nItemCount = itemCount;
        m_RecallInfo.m_bIncludeDead = includeDead;
    }

    const int z = runtime::Win64CvttDoubleToInt32(caller->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(caller->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(caller->GetPos().x);
    GetSocket()->Send(
        "cdddSdddddd", 0xf3, 0x732, 2, 0xc, caller->GetName(), 7, x, y, z, 30000, static_cast<int>(caller->m_nId)
    );
}

void User::CallPCAnswer(const L2ServerLong requestId, const bool answer) {
    // Current L2Server 0x00bad988, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t recallerSid;
    std::uint32_t recalledTime;
    std::int32_t itemClassId;
    std::int32_t itemCount;
    bool includeDead;
    {
        L2_GUARD(m_RecallLock);
        recallerSid = m_RecallInfo.m_nRecallerSID;
        recalledTime = m_RecallInfo.m_nRecalledTime;
        itemClassId = m_RecallInfo.m_nItemClassID;
        itemCount = m_RecallInfo.m_nItemCount;
        includeDead = m_RecallInfo.m_bIncludeDead;
    }

    User* caller = nullptr;
    CallPCResult result = CPR_ETC;
    if (answer) {
        if (requestId != recallerSid) {
            return;
        }

        CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(recallerSid));
        caller = object && object->IsPC() ? object->CastUser() : nullptr;
        if (caller != nullptr) {
            if (runtime::GetTickCount() - recalledTime > 40000U) {
                result = static_cast<CallPCResult>(2);
            } else if (IsCallPCEnable(caller, this, includeDead, result)) {
                if (itemClassId > 0 && itemCount > 0) {
                    const bool hasItem = [&] {
                        L2_WRITE_GUARD(m_inv);
                        return m_inv.HaveItemByClassId(itemClassId, itemCount, false, false);
                    }();
                    if (!hasItem) {
                        result = static_cast<CallPCResult>(3);
                        GetSocket()->Send(
                            "chccd", 0x62, SystemMessageId::SUMMONING_REQUIRED_ITEM_MISSING, 1, 3, itemClassId
                        );
                    } else {
                        ItemManip request(
                            0, itemClassId, static_cast<std::int64_t>(runtime::SubtractInt32Wrapping(0, itemCount)), 0, 0,
                            0, true
                        );
                        L2_GUARD(m_ManipulateQueueLock);
                        CPacketHelperEx<20480> packet;
                        etcMsg message = SaveEtcMsg(
                            false, false, static_cast<std::int32_t>(result), recallerSid, 0, std::u16string(),
                            std::u16string(), recalledTime
                        );
                        AtomicJob* const job = new CAtomicCallPC(static_cast<std::uint32_t>(m_nId), recallerSid);
                        ManipulateQueue queue = SaveManipulateQueue(
                            ManipulateType::MANI_CALL_PC, 0, 1, &request, job, 0, packet, false, ItemGetReason::Etc, message
                        );

                        const ManiItemResultType queueResult = _SetManiItemJob(queue);
                        if (queueResult == MRT_NOJOB) {
                            if (!_HandleItemManipulation(queue)) {
                                job->Destroy();
                            }
                        } else if (queueResult == MRT_JOBMAP) {
                            job->Destroy();
                            SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                        } else if (queueResult == MRT_JOBNOTMAP) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                            //     L"user_skill.cpp",
                            //     0x7a3)
                            // L2_LOG_ERROR(Log,
                            //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                            //     L"user_skill.cpp",
                            //     0x7a3)
                            // Noise: the item job was queued normally. Enqueueing is neither an error nor proof that the
                            // recall completed.
                        } else {
                            job->Destroy();
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                            //     L"user_skill.cpp",
                            //     0x7a9)
                            // L2_LOG_ERROR(Log,
                            //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                            //     L"user_skill.cpp",
                            //     0x7a9)
                            // L2_LOG_ERROR(Log,
                            //     "Character %u ('%s'): cannot queue recall item consumption: queue returned %d. Atomic
                            //     recall job destroyed.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(),
                            //     static_cast<int>(queueResult))
                            L2_LOG_ERROR(
                                Log,
                                "Character %u ('%s'): cannot queue recall item consumption: queue returned %d; atomic "
                                "recall job destroyed.",
                                static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(),
                                static_cast<int>(queueResult)
                            );
                        }
                        return;
                    }
                } else {
                    AtomicJob* const job = new CAtomicCallPC(static_cast<std::uint32_t>(m_nId), recallerSid);
                    static_cast<void>(job->Do());
                    job->Destroy();
                    return;
                }
            }
        }
    } else {
        result = static_cast<CallPCResult>(1);
    }

    {
        L2_GUARD(m_RecallLock);
        if (recallerSid == m_RecallInfo.m_nRecallerSID && recalledTime == m_RecallInfo.m_nRecalledTime) {
            m_RecallInfo.m_nRecallerSID = 0;
            m_RecallInfo.m_nRecalledTime = 0;
            m_RecallInfo.m_nItemClassID = 0;
            m_RecallInfo.m_nItemCount = 0;
            m_RecallInfo.m_bIncludeDead = false;
        }
    }

    if (caller != nullptr) {
        const FVector position = caller->GetPos();
#if defined(_WIN32)
        const wchar_t* const callerAccount = reinterpret_cast<const wchar_t*>(caller->m_sAccountName.data());
        const wchar_t* const recalledAccount = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
        std::array<wchar_t, 16> callerAccountStorage{};
        std::array<wchar_t, 16> recalledAccountStorage{};
        const wchar_t* const callerAccount = UserSkillAccountName(caller->m_sAccountName, callerAccountStorage);
        const wchar_t* const recalledAccount = UserSkillAccountName(m_sAccountName, recalledAccountStorage);
#endif
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
        //     L"%s,%s,%s,%s,",
        //     static_cast<std::int64_t>(0x373),
        //     static_cast<std::int64_t>(caller->GetDBID()),
        //     static_cast<std::int64_t>(caller->GetAccountID()),
        //     static_cast<std::int64_t>(GetDBID()),
        //     static_cast<std::int64_t>(GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(caller->m_data->m_Race),
        //     static_cast<std::int64_t>(caller->m_data->m_Sex),
        //     static_cast<std::int64_t>(caller->m_data->m_nClass),
        //     static_cast<std::int64_t>(caller->m_data->m_nLevel),
        //     static_cast<std::int64_t>(m_data->m_Race),
        //     static_cast<std::int64_t>(m_data->m_Sex),
        //     static_cast<std::int64_t>(m_data->m_nClass),
        //     static_cast<std::int64_t>(m_data->m_nLevel),
        //     static_cast<std::int64_t>(result),
        //     caller->GetName(),
        //     callerAccount,
        //     GetName(),
        //     recalledAccount)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
            u"%s,%s,%s,%s,",
            static_cast<std::int64_t>(0x373), static_cast<std::int64_t>(caller->GetDBID()),
            static_cast<std::int64_t>(caller->GetAccountID()), static_cast<std::int64_t>(GetDBID()),
            static_cast<std::int64_t>(GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(caller->m_data->m_Race), static_cast<std::int64_t>(caller->m_data->m_Sex),
            static_cast<std::int64_t>(caller->m_data->m_nClass), static_cast<std::int64_t>(caller->m_data->m_nLevel),
            static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
            static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel),
            static_cast<std::int64_t>(result), caller->GetName(), callerAccount, GetName(), recalledAccount
        );
    }
}

void User::CallPCVerified(const L2ServerLong recallerRequestId) {
    // Current L2Server 0x00bab89c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t recallerSid;
    std::uint32_t recalledTime;
    {
        L2_GUARD(m_RecallLock);
        recallerSid = m_RecallInfo.m_nRecallerSID;
        recalledTime = m_RecallInfo.m_nRecalledTime;
    }

    if (recallerRequestId != recallerSid) {
        return;
    }

    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(recallerSid));
    User* const caller = object && object->IsPC() ? object->CastUser() : nullptr;
    if (caller != nullptr) {
        {
            FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
                this, summon, if (summon->m_data->m_Race == 0x18) { DespawnSummon(summon, true, false); }
            );
        }

        const FVector position = caller->GetPos();
        StopMove();
        DoNothing();
        static_cast<void>(TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), GetInZoneID(), 0x10000,
            TeleportSourceType::UserCallPCVerified, nullptr, 0
        ));

#if defined(_WIN32)
        const wchar_t* const callerAccount = reinterpret_cast<const wchar_t*>(caller->m_sAccountName.data());
        const wchar_t* const recalledAccount = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
        std::array<wchar_t, 16> callerAccountStorage{};
        std::array<wchar_t, 16> recalledAccountStorage{};
        const wchar_t* const callerAccount = UserSkillAccountName(caller->m_sAccountName, callerAccountStorage);
        const wchar_t* const recalledAccount = UserSkillAccountName(m_sAccountName, recalledAccountStorage);
#endif
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
        //     L"%s,%s,%s,%s,",
        //     static_cast<std::int64_t>(0x373),
        //     static_cast<std::int64_t>(caller->GetDBID()),
        //     static_cast<std::int64_t>(caller->GetAccountID()),
        //     static_cast<std::int64_t>(GetDBID()),
        //     static_cast<std::int64_t>(GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(caller->m_data->m_Race),
        //     static_cast<std::int64_t>(caller->m_data->m_Sex),
        //     static_cast<std::int64_t>(caller->m_data->m_nClass),
        //     static_cast<std::int64_t>(caller->m_data->m_nLevel),
        //     static_cast<std::int64_t>(m_data->m_Race),
        //     static_cast<std::int64_t>(m_data->m_Sex),
        //     static_cast<std::int64_t>(m_data->m_nClass),
        //     static_cast<std::int64_t>(m_data->m_nLevel),
        //     std::int64_t{0},
        //     caller->GetName(),
        //     callerAccount,
        //     GetName(),
        //     recalledAccount)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
            u"%s,%s,%s,%s,",
            static_cast<std::int64_t>(0x373), static_cast<std::int64_t>(caller->GetDBID()),
            static_cast<std::int64_t>(caller->GetAccountID()), static_cast<std::int64_t>(GetDBID()),
            static_cast<std::int64_t>(GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(caller->m_data->m_Race), static_cast<std::int64_t>(caller->m_data->m_Sex),
            static_cast<std::int64_t>(caller->m_data->m_nClass), static_cast<std::int64_t>(caller->m_data->m_nLevel),
            static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
            static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel), std::int64_t{0},
            caller->GetName(), callerAccount, GetName(), recalledAccount
        );
    }

    {
        L2_GUARD(m_RecallLock);
        if (recallerSid == m_RecallInfo.m_nRecallerSID && recalledTime == m_RecallInfo.m_nRecalledTime) {
            m_RecallInfo.m_nRecallerSID = 0;
            m_RecallInfo.m_nRecalledTime = 0;
            m_RecallInfo.m_nItemClassID = 0;
            m_RecallInfo.m_nItemCount = 0;
            m_RecallInfo.m_bIncludeDead = false;
        }
    }
}

void User::UnequipWeapon(bool force) {
    // Current L2Server 0x00ba7410, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = GetEquipedWeapon();
    if (weapon == nullptr) {
        return;
    }
    if (force && weapon->index != -1) {
        m_SkillMod.m_nBlockedWeaponIdx = weapon->index;
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & weapon->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    UnequipItem(slot, false);
}

void User::UnequipArmor(SlotType slotType) {
    // Current L2Server 0x00ba7500, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_nSlotIndex[static_cast<int>(slotType)] != -1 && slotType == ST_CHEST) {
        m_SkillMod.m_nBlockedChestArmorIdx = m_data->m_nSlotIndex[ST_CHEST];
        UnequipItem(ST_CHEST, false);
    }
}

void User::RestoreUnequipedWeapon() {
    // Current L2Server 0x00baa564, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (m_SkillMod.m_nBlockedWeaponIdx == -1) {
        return;
    }

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_SkillMod.m_nBlockedWeaponIdx);
    if (item && m_inv.Have(item->m_data->index)) {
        EquipItem(item.get(), ST_MAX);
    }
    m_SkillMod.m_nBlockedWeaponIdx = -1;
}

void User::RestoreUnequipedArmor(SlotType slotType) {
    // Current L2Server 0x00baa6b8, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (slotType != ST_CHEST || m_SkillMod.m_nBlockedChestArmorIdx == -1) {
        return;
    }

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_SkillMod.m_nBlockedChestArmorIdx);
    if (item && m_inv.Have(item->m_data->index)) {
        EquipItem(item.get(), ST_MAX);
    }
    m_SkillMod.m_nBlockedChestArmorIdx = -1;
}

bool User::AcquireSkillByItem(const CSkillInfo* skillInfo) {
    // Current L2Server 0x00ba75d0, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (skillInfo != nullptr) {
        AcquireSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, false, false);
    }
    return skillInfo != nullptr;
}

bool User::PacketAcceptable(int packetNo) {
    // Current L2Server 0x00ba6664.
    L2_GUARD_FUNCTION();

    if (!m_bIsObserving) {
        if (!m_pWOlympiadUser->IsObserver() && !m_PledgeGameUser.IsObserver()) {
            if (m_SkillDurationMod.m_nBlockCanControllCount != 0) {
                return !aPacketBlockTable_CannotControl[packetNo];
            }

            if (m_data->m_nWorldId != g_nWorldId && !aPacketAllowTable_ForRaidUser[packetNo]) {
                const std::uint32_t now = runtime::GetTickCount();
                if (now - m_dRaidBlockMsgInterval > 5000U) {
                    SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
                    m_dRaidBlockMsgInterval = now;
                }
                return false;
            }
            return true;
        }

        if (packetNo == 0x0f || packetNo == 0x59 || packetNo == 0x47 || !aPacketBlockTable_CannotControl[packetNo]) {
            return true;
        }
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_OBSERVING);
        return false;
    }

    if (packetNo == 0x1f || !aPacketBlockTable_CannotControl[packetNo]) {
        return true;
    }
    if (packetNo != 0x59) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_OBSERVING);
    }
    return false;
}

bool User::PacketExAcceptable(int packetNo) {
    // Current L2Server 0x00ba682c.
    L2_GUARD_FUNCTION();
    if (m_data->m_nWorldId == g_nWorldId || aPacketExAllowTable_ForRaidUser[packetNo]) {
        return true;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (now - m_dRaidBlockMsgInterval > 5000U) {
        SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
        m_dRaidBlockMsgInterval = now;
    }
    return false;
}

void User::SendAcquiredSkillToDB(int skillId, int skillLevel, int skillSubLevel, bool saveToDatabase) {
    // Current L2Server 0x00ba6af8.
    L2_GUARD_FUNCTION();
    db.SendSaveCharacterInfo(this, false, true);
    if (!g_SkillAcquireDB.IsMatchSkillAcquireType(ESAT_HONORS, skillId, skillLevel)) {
        db.SendAcquiredSkill(*this, skillId, skillLevel, skillSubLevel, saveToDatabase);
    }
}

void User::ResetAllEnchantSkill(CUserSocket* socket) {
    // Current L2Server 0x00bad0b8, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    std::map<int, AcquiredSkillInfo> enchantSkills;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        for (const auto& acquiredSkill : m_AcquiredSkills) {
            const AcquiredSkillInfo& info = acquiredSkill.second;
            if (g_SkillEnchantDB.GetSkillEnchantInfo(info.m_nId, info.m_nLevel, info.m_nSubLevel) != nullptr) {
                enchantSkills.emplace(info.m_nId, AcquiredSkillInfo(info.m_nId, info.m_nLevel, info.m_nSubLevel, 0, true));
            }
        }
    }

    if (enchantSkills.empty()) {
        socket->SendSystemMessage(u"SYS", u"that user doesn't have enchant skill.");
        return;
    }

    for (const auto& enchantSkill : enchantSkills) {
        const AcquiredSkillInfo& info = enchantSkill.second;
        SkillEnchantInfo* const enchantInfo =
            g_SkillEnchantDB.GetSkillEnchantInfo(info.m_nId, info.m_nLevel, info.m_nSubLevel);
        if (enchantInfo == nullptr) {
            continue;
        }

        {
            L2_GUARD(*m_lock);
            const bool acquired = AcquireSkill(enchantInfo->nSkillID, info.m_nLevel, 0, true, true);
            if (acquired && info.m_bSaveToDB) {
                SendAcquiredSkillToDB(enchantInfo->nSkillID, info.m_nLevel, 0, true);
            }
        }
        ValidateChanged(std::int64_t{VB_MAX});
    }

    ParamChanged(std::int64_t{PCB_USER});
    std::array<char16_t, 512> message{};
    FormatUtf16To(message.data(), message.size(), u"Reset %d enchant skills.", static_cast<int>(enchantSkills.size()));
    socket->SendSystemMessage(u"SYS", message.data());
}

void User::SetSkillAll(int) {
    // Current L2Server 0x00bab274 ignores the supplied class and uses the
    // character's current class from shared data.
    L2_GUARD_FUNCTION();
    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(m_data->m_nClass);

    {
        L2_GUARD(*m_lock);
        DisableSetItemSkill();
        AcquireSkills(skills, false, false);

        {
            L2_GUARD(m_AcquiredSkillsSect);
            for (const auto& entry : m_AcquiredSkills) {
                const AcquiredSkillInfo& skill = entry.second;
                if (skill.m_bSaveToDB) {
                    SendAcquiredSkillToDB(entry.first, skill.m_nLevel, skill.m_nSubLevel, true);
                }
            }
        }

        ValidateSetItem(ST_MAX);
        ValidateNewSetItem();
        ValidateShapeShiftedSetItem();
    }
    ValidateChanged(VB_MAX);
}

void User::AcquireAlchemySkill(AcquiredAlchemySkillInfo& skill, int type) {
    // Current L2Server 0x00babd68.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= 2U) {
        return;
    }

    L2_GUARD(m_AlchemySkillsLock[type]);
    const int skillId = skill.pSkillInfo->m_nSkillId;
    const auto inserted = m_AlchemySkills[type].emplace(skillId, skill);
    if (!inserted.second) {
        inserted.first->second.pSkillInfo = skill.pSkillInfo;
        inserted.first->second.bMaster = skill.bMaster;
    }
    if (type == 0) {
        m_nAlchemyRollySkillId = skillId;
        m_nAlchemyRollyLev = skill.pSkillInfo->m_nLevel;
    }
}

void User::DeleteAlchemySkill(int skillId, int type) {
    // Current L2Server 0x00bab66c.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(type) >= 2U) {
        return;
    }

    L2_GUARD(m_AlchemySkillsLock[type]);
    const auto found = m_AlchemySkills[type].find(skillId);
    if (found != m_AlchemySkills[type].end()) {
        m_AlchemySkills[type].erase(found);
    }
    if (type == 0) {
        m_nAlchemyRollySkillId = 0;
        m_nAlchemyRollyLev = 0;
    }
}

bool User::IsLearnableAbilitySkill(const int skillId, const int skillLevel, const int type, const int acquiredCount) {
    // Current L2Server 0x00ba845c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();

    PotentialSkillAcquireInfo* const potential = CPotentialSkillDB::Inst().IsExistPotential(skillId);
    if (potential == nullptr) {
        // Log.Add(LOG_ERROR, L"Not Exist Potential Skill")
        // L2_LOG_ERROR(Log, L"Not Exist Potential Skill")
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot learn ability skill %d level %d: potential-skill definition is missing.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), skillId, skillLevel)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot learn ability skill %d level %d: potential-skill definition is missing.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), skillId, skillLevel
        );
        return false;
    }
    if (potential->etype != type) {
        return false;
    }

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, 0), this);
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid PotentailSkill [%d][%d], "
        //     L"Failed to get skillinfo",
        //     L"user_skill.cpp",
        //     0xdb2,
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid PotentailSkill [%d][%d], "
        //     L"Failed to get skillinfo",
        //     L"user_skill.cpp",
        //     0xdb2,
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): cannot learn ability skill %d level %d: skill lookup failed.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), skillId, skillLevel)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): cannot learn ability skill %d level %d: skill lookup failed.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), skillId, skillLevel
        );
        return false;
    }

    return potential->nRequireLev <= m_data->m_nLevel &&
        (potential->nRequireSkillId == 0 || HaveSkill(potential->nRequireSkillId, potential->nRequireiSkillLev)) &&
        potential->nRequireCount <= acquiredCount;
}

bool User::IsLearnableSkill(const SkillAcquireInfo& acquireInfo, bool sendMessage) {
    // Current L2Server 0x00ba8fc8.
    L2_GUARD_FUNCTION();

    bool enoughSp = false;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        if (m_data->m_nLevel < acquireInfo.nGetLevel) {
            return false;
        }
        enoughSp = acquireInfo.nLevelUP_SP <= m_data->m_nSP;
    }

    bool missingItem = false;
    {
        L2_GUARD(*m_lock);
        for (std::size_t index = 0; index < acquireInfo.vNeededItem.size(); ++index) {
            if (m_inv.GetItemAmount(runtime::CheckedVectorAt(acquireInfo.vNeededItem, index), 0, true) <
                runtime::CheckedVectorAt(acquireInfo.vNeededItemCount, index)) {
                missingItem = true;
                break;
            }
        }
    }

    if (enoughSp && !missingItem) {
        {
            L2_GUARD(m_AcquiredSkillsSect);
            for (const CSkillInfo* const prerequisite : acquireInfo.m_PrerequisiteSkills) {
                if (!_HaveSkill(prerequisite->m_nSkillId, prerequisite->m_nLevel)) {
                    if (sendMessage) {
                        SendSystemMessage(SystemMessageId::CANNOT_LEARN_SKILL_PREREQUISITE_SKILL_MISSING);
                    }
                    return false;
                }
            }
        }

        for (const int questId : acquireInfo.vNeededQuest) {
            if (m_data->GetOneTimeFlag(questId)) {
                return true;
            }
        }
        if (!acquireInfo.vNeededQuest.empty() && sendMessage) {
            SendSystemMessage(SystemMessageId::SKILL_LEARNING_REQUIRED_QUEST_NOT_COMPLETED);
            return false;
        }
        return true;
    }

    if (sendMessage) {
        if (enoughSp) {
            if (missingItem) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEMS_TO_LEARN_SKILL);
            }
        } else if (missingItem) {
            SendSystemMessage(SystemMessageId::NOT_ENOUGH_SP_AND_ITEMS_TO_LEARN_SKILL);
        } else {
            SendSystemMessage(SystemMessageId::NOT_ENOUGH_SP_TO_LEARN_SKILL);
        }
    }
    return false;
}

bool User::CheckForSkillAcquireList(EtcSkillAcquireType type, const SkillAcquireInfo& acquireInfo) {
    // Current L2Server 0x00baac0c.
    L2_GUARD_FUNCTION();
    if (type == ESAT_PLEDGE || type == ESAT_SUB_PLEDGE) {
        return false;
    }

    bool consecutive = false;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        const auto acquired = m_AcquiredSkills.find(acquireInfo.nSkillID);
        if (acquired == m_AcquiredSkills.end()) {
            const int minimumLevel = type == ESAT_NORMAL
                ? g_SkillAcquireDB.GetMinimumLevel(m_data->m_nClass, acquireInfo.nSkillID)
                : g_SkillAcquireDB.GetEtcMinimumLevel(type, acquireInfo.nSkillID);
            consecutive = acquireInfo.nSkillLevel <= minimumLevel;
        } else {
            consecutive = acquireInfo.nSkillLevel == acquired->second.m_nLevel + 1;
        }
    }

    if (!consecutive) {
        return false;
    }
    return acquireInfo.m_RaceConditions.empty() ||
        acquireInfo.m_RaceConditions.find(m_data->m_Race) != acquireInfo.m_RaceConditions.end();
}

bool User::SendNewSkillByLevelUp() {
    // Current L2Server 0x00baae64.
    L2_GUARD_FUNCTION();
    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(ESAT_NORMAL, m_data->m_nClass);
    for (const SkillAcquireInfo* const acquireInfo : skills) {
        if (acquireInfo == nullptr || acquireInfo->m_bResidenceSkill || acquireInfo->m_bMentorMenteeSkill ||
            acquireInfo->m_bExcludedFromListOfSkillsToLearn || acquireInfo->nGetLevel > m_data->m_nLevel ||
            !CheckForSkillAcquireList(ESAT_NORMAL, *acquireInfo) || acquireInfo->nSkillLevel != 1) {
            continue;
        }
        GetSocket()->Send("ch", 0xfe, 0xfd);
        return true;
    }
    return false;
}

void User::SendTotalSkillEnchantCountToNpcServer(const int requestId) {
    // Current L2Server 0x00bab440.
    L2_GUARD_FUNCTION();

    std::int32_t rewardSp = 0;
    std::int32_t rewardItemCount = 0;
    {
        L2_GUARD(m_AcquiredSkillsSect);
        for (const auto& acquiredSkill : m_AcquiredSkills) {
            const AcquiredSkillInfo& info = acquiredSkill.second;
            if (info.m_nLevel > 100) {
                rewardSp = runtime::AddInt32Wrapping(
                    rewardSp, g_SkillEnchantDB.GetRewardSP(acquiredSkill.first, info.m_nLevel, GetClass())
                );
                rewardItemCount = runtime::AddInt32Wrapping(
                    rewardItemCount, g_SkillEnchantDB.GetRewardItemCount(acquiredSkill.first, info.m_nLevel, Level())
                );
            }
        }
    }

    if (rewardSp < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GetTotal] reward sp invalid! [%s][%d]",
        //     GetName(),
        //     rewardSp)
        // L2_LOG_ERROR(Log,
        //     L"[GetTotal] reward sp invalid! [%s][%d]",
        //     GetName(),
        //     rewardSp)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): skill-enchantment refund SP is negative (%d). Reporting 0 to the NPC server.",
        //     static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), rewardSp)
        L2_LOG_ERROR(
            Log, "Character %u ('%s'): skill-enchantment refund SP is negative (%d); reporting 0 to the NPC server.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), rewardSp
        );
        rewardSp = 0;
    }
    if (rewardItemCount < 0 || rewardItemCount > 8000) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"reward item count invalid! [%s][%d]",
        //     GetName(),
        //     rewardItemCount)
        // L2_LOG_ERROR(Log,
        //     L"reward item count invalid! [%s][%d]",
        //     GetName(),
        //     rewardItemCount)
        // L2_LOG_ERROR(Log,
        //     "Character %u ('%s'): skill-enchantment refund item count %d is outside 0..8000. Reporting 0 to the NPC
        //     server.", static_cast<unsigned>(GetDBID()), WideToUtf8(GetName()).c_str(), rewardItemCount)
        L2_LOG_ERROR(
            Log,
            "Character %u ('%s'): skill-enchantment refund item count %d is outside 0..8000; reporting 0 to the NPC server.",
            static_cast<unsigned>(GetDBID()), Utf16ToUtf8(GetName()).c_str(), rewardItemCount
        );
        rewardItemCount = 0;
    }

    g_npcserver.Send("cdddd", 0xb3, requestId, static_cast<std::int32_t>(m_nId), rewardSp, rewardItemCount);
}

bool User::GatherSkillAcquireList(
    std::map<CSkillKey, AcquireInfo>& acquireList,
    int& nextSkillAcquireLevel,
    EtcSkillAcquireType acquireType,
    bool includePreviousSkills
) {
    // Current L2Server 0x00bac3a8.
    L2_GUARD_FUNCTION();
    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(acquireType, m_data->m_nClass);
    nextSkillAcquireLevel = 99;

    int skillAcquireLevel;
    if (acquireType == ESAT_PLEDGE || acquireType == ESAT_SUB_PLEDGE) {
        CPledge* const pledge = GetPledge();
        skillAcquireLevel = pledge != nullptr ? static_cast<int>(pledge->GetPledgeLevel()) : -1;
    } else {
        skillAcquireLevel = m_data->m_nLevel;
    }
    if (skillAcquireLevel == -1) {
        return false;
    }

    for (SkillAcquireInfo* const skill : skills) {
        if (skill == nullptr || skill->m_bResidenceSkill || skill->m_bMentorMenteeSkill ||
            skill->m_bExcludedFromListOfSkillsToLearn ||
            (m_data->m_nSubJobId != 0 && !m_data->m_IsDualClass && skill->nGetLevel >= 0x51)) {
            continue;
        }

        if (!skill->bAutoGet && skillAcquireLevel < skill->nGetLevel && skill->nGetLevel < nextSkillAcquireLevel) {
            nextSkillAcquireLevel = skill->nGetLevel;
        }

        if (skillAcquireLevel < skill->nGetLevel && acquireType != ESAT_NORMAL && acquireType != ESAT_ALCHEMY) {
            continue;
        }

        const CSkillKey key(skill->nSkillID, skill->nSkillLevel, 0);
        const auto existing = acquireList.find(key);
        if (existing != acquireList.end()) {
            existing->second.max_level = std::max(existing->second.max_level, skill->nSkillLevel);
        }

        if (!CheckForSkillAcquireList(acquireType, *skill)) {
            continue;
        }

        AcquireInfo info;
        info.m_Id = skill->nSkillID;
        info.m_Level = skill->nSkillLevel;
        info.max_level = skill->nSkillLevel;
        info.m_nSubLevel = 0;
        info.sp_consume = skill->nLevelUP_SP;
        info.m_ItemConsume = !skill->vNeededItem.empty();
        info.m_MaximumPledgeType = acquireType == ESAT_SUB_PLEDGE ? skill->m_MaximumPledgeType : ACADEMY;
        info.m_pAcquireInfo = skill;
        acquireList[key] = info;
    }

    if (includePreviousSkills) {
        LeaveOnlyLearnableSkills(*this, acquireList);
    }
    return true;
}

void User::_PackingSkillList(SendSkillListData& data, const CSkillInfo* skillInfo, SkillType skillType) {
    // Current L2Server 0x00ba69e0.
    ++data.m_Count;
    const bool locked = IsLockedSkill(skillInfo->m_nSkillId) != 0;
    const bool enchantable =
        g_SkillEnchantDB.IsEnchantableSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
    const int packedLevel = runtime::Int32FromBits(
        (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skillInfo->m_nLevel)
    );
    const int assembled = ::Assemble(
        data.m_Packet.data() + data.m_Length, 0x4f9c - data.m_Length, "ddddcc", static_cast<int>(skillType), packedLevel,
        skillInfo->m_nSkillId, skillInfo->m_nReuseDelayGroup, locked, enchantable
    );
    if (assembled > 0) {
        data.m_Length += assembled;
    }
}

void User::SendSkillList(CUserSocket* socket, bool includePreviousSkills, int unknownSkillId) {
    // Current L2Server 0x00bad5e4.
    L2_GUARD_FUNCTION();
    if (includePreviousSkills) {
        m_ShortCut.SendShortCutInfo();
        SendBasicActionList();
    }

    SendSkillListData data{};
    {
        L2_GUARD(m_SkillListSect);
        for (const auto& entry : m_ActiveSkills) {
            const CSkillInfo* const skillInfo = entry.second;
            if (!skillInfo->m_bIsDoubleCasting && !skillInfo->m_isClassChangeSkill && !skillInfo->m_bHairAccFlag) {
                _PackingSkillList(data, skillInfo, SKT_ACTIVE);
            }
        }
        for (const CSkillInfo* const skillInfo : m_PassiveSkills) {
            if (!skillInfo->m_bIsDoubleCasting && skillInfo->m_OperateType != SKILL_RACE_SPECIAL_PASSIVE) {
                _PackingSkillList(data, skillInfo, SKT_PASSIVE);
            }
        }
        for (const auto& entry : m_ToggleSkills) {
            const CSkillInfo* const skillInfo = entry.second.first;
            if (!skillInfo->m_bIsDoubleCasting) {
                _PackingSkillList(data, skillInfo, SKT_ACTIVE);
            }
        }
        for (const auto& entry : m_AuraSkills) {
            const CSkillInfo* const skillInfo = entry.second.first;
            if (!skillInfo->m_bIsDoubleCasting) {
                _PackingSkillList(data, skillInfo, SKT_ACTIVE);
            }
        }
    }

    if (socket == nullptr) {
        GetSocket()->Send("cdbd", 0x5f, data.m_Count, data.m_Length, data.m_Packet.data(), unknownSkillId);
        SendSkillAcquireList(ESAT_NORMAL, false);
    } else {
        socket->Send("cSdb", 0x97, GetName(), data.m_Count, data.m_Length, data.m_Packet.data());
    }
}

void User::SendSkillAcquireList(EtcSkillAcquireType acquireType, bool sendNoSkillMessage) {
    // Current L2Server 0x00baca48.
    L2_GUARD_FUNCTION();
    std::map<CSkillKey, AcquireInfo> acquireList;
    int nextSkillAcquireLevel = 0;
    SetAcquireSkillListSendClass(acquireType);
    if (!GatherSkillAcquireList(acquireList, nextSkillAcquireLevel, acquireType, sendNoSkillMessage)) {
        return;
    }

    CUserSocket* const socket = GetSocket();
    if (acquireList.empty()) {
        if (acquireType == ESAT_NORMAL) {
            socket->Send("c", 0x94);
            GetSocket()->Send("ch", 0x90, 0);
        } else {
            socket->Send("chhh", 0xfe, 0xfa, static_cast<int>(static_cast<std::int16_t>(acquireType)), 0);
        }
        if (sendNoSkillMessage) {
            GetSocket()->SendSystemMessage(SystemMessageId::NO_MORE_SKILLS_TO_LEARN);
        }
        return;
    }

    std::array<char, 0x4f9c> packet;
    int packetLength = 0;
    if (acquireType == ESAT_NORMAL) {
        int skillCount = 0;
        for (const auto& entry : acquireList) {
            const AcquireInfo& info = entry.second;
            SkillAcquireInfo* const skill = info.m_pAcquireInfo;
            if (skill->bAutoGet) {
                continue;
            }

            const int acquiredSubLevel = GetAcquiredSkillSubLevel(info.m_Id);
            const int packedLevel = runtime::Int32FromBits(
                (static_cast<std::uint32_t>(acquiredSubLevel) << 16U) | static_cast<std::uint16_t>(info.m_Level)
            );
            const int neededItemCount = static_cast<int>(static_cast<std::int8_t>(skill->vNeededItem.size()));
            const bool protocol152 = runtime::client_protocol::NegotiatedRevision(socket) ==
                runtime::client_protocol::kNorthAmericanRevision;
            // NA 5.0.0.35 Engine 0x2034a503 decodes dhhQcccc. Its
            // NWindow 0x1054f339 publishes the extra byte as IsNewSkill,
            // before RequiredItemTotalCnt. The current P162 omits it.
            if (protocol152) {
                packetLength += ::Assemble(
                    packet.data() + packetLength, 0x4f9c - packetLength, "ddQcccc", info.m_Id, packedLevel, info.sp_consume,
                    static_cast<int>(static_cast<std::int8_t>(skill->nGetLevel)),
                    static_cast<int>(static_cast<std::int8_t>(skill->m_GetMainDualClassLevel)),
                    GetAcquiredSkillLevel(info.m_Id) == -1, neededItemCount
                );
            } else {
                packetLength += ::Assemble(
                    packet.data() + packetLength, 0x4f9c - packetLength, "ddQccc", info.m_Id, packedLevel, info.sp_consume,
                    static_cast<int>(static_cast<std::int8_t>(skill->nGetLevel)),
                    static_cast<int>(static_cast<std::int8_t>(skill->m_GetMainDualClassLevel)), neededItemCount
                );
            }

            for (std::size_t index = 0; index < skill->vNeededItem.size(); ++index) {
                packetLength += ::Assemble(
                    packet.data() + packetLength, 0x4f9c - packetLength, "dQ",
                    runtime::CheckedVectorAt(skill->vNeededItem, index), runtime::CheckedVectorAt(skill->vNeededItemCount, index)
                );
            }

            std::uint8_t prerequisiteCount = 0;
            for (const CSkillInfo* const prerequisite : skill->m_PrerequisiteSkills) {
                if (GetAcquiredSkillLevel(prerequisite->m_nSkillId) != -1) {
                    prerequisiteCount = static_cast<std::uint8_t>(prerequisiteCount + 1U);
                }
            }
            packetLength +=
                ::Assemble(packet.data() + packetLength, 0x4f9c - packetLength, "c", static_cast<int>(prerequisiteCount));

            for (const CSkillInfo* const prerequisite : skill->m_PrerequisiteSkills) {
                const int subLevel = GetAcquiredSkillSubLevel(prerequisite->m_nSkillId);
                if (GetAcquiredSkillLevel(prerequisite->m_nSkillId) != -1) {
                    const int packedPrerequisiteLevel = runtime::Int32FromBits(
                        (static_cast<std::uint32_t>(subLevel) << 16U) | static_cast<std::uint16_t>(prerequisite->m_nLevel)
                    );
                    packetLength += ::Assemble(
                        packet.data() + packetLength, 0x4f9c - packetLength, "dd", prerequisite->m_nSkillId,
                        packedPrerequisiteLevel
                    );
                }
            }
            ++skillCount;
        }

        GetSocket()->Send("chb", 0x90, static_cast<int>(static_cast<std::int16_t>(skillCount)), packetLength, packet.data());
        return;
    }

    for (const auto& entry : acquireList) {
        const AcquireInfo& info = entry.second;
        SkillAcquireInfo* const skill = info.m_pAcquireInfo;
        if (acquireType == ESAT_SUB_PLEDGE) {
            packetLength += ::Assemble(
                packet.data() + packetLength, 0x4f9c - packetLength, "dhhcQch", info.m_Id,
                static_cast<int>(static_cast<std::int16_t>(info.m_Level)),
                static_cast<int>(static_cast<std::int16_t>(info.max_level)),
                static_cast<int>(static_cast<std::int8_t>(skill->nGetLevel)), info.sp_consume,
                static_cast<int>(info.m_ItemConsume), static_cast<int>(static_cast<std::int16_t>(info.m_MaximumPledgeType))
            );
        } else {
            packetLength += ::Assemble(
                packet.data() + packetLength, 0x4f9c - packetLength, "dhhcQc", info.m_Id,
                static_cast<int>(static_cast<std::int16_t>(info.m_Level)),
                static_cast<int>(static_cast<std::int16_t>(info.max_level)),
                static_cast<int>(static_cast<std::int8_t>(skill->nGetLevel)), info.sp_consume,
                static_cast<int>(info.m_ItemConsume)
            );
        }
    }

    GetSocket()->Send(
        "chhhb", 0xfe, 0xfa, static_cast<int>(static_cast<std::int16_t>(acquireType)),
        static_cast<int>(static_cast<std::int16_t>(acquireList.size())), packetLength, packet.data()
    );
}

bool User::BasicAction(
    const ServerTriggeredUserAction action, const bool controlPressed, const bool shiftPressed, const StopType stopType
) {
    // Current L2Server 0x00ba6d5c.
    L2_GUARD_FUNCTION();

    if (stopType != STP_MAX && stopType != m_data->m_StopMode) {
        return false;
    }
    return g_UserBasicAction.HandleServerTriggeredAction(this, action, controlPressed, shiftPressed);
}

bool User::IsValidAction(const int actionId) {
    // Current L2Server.exe 0x00bac214, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_nWorldId == g_nWorldId) {
        if (IsTransformed(TransformType::Max) == nullptr) {
            if (actionId == 0 && m_bFlyMoving) {
                SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE);
                return false;
            }
            return g_UserBasicAction.IsValid(actionId);
        }
        return m_transform->IsValidAction(actionId);
    }
    return RaidAllowAction::Inst().IsValidAction(actionId);
}

bool User::BasicAction(const int actionId, const bool controlPressed, const bool shiftPressed, const StopType stopType) {
    // Current L2Server.exe 0x00bac600, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (!IsValidAction(actionId)) {
        return false;
    }
    if (stopType != STP_MAX && stopType != m_data->m_StopMode) {
        return false;
    }
    return g_UserBasicAction.HandleAction(this, actionId, controlPressed, shiftPressed);
}

void User::ChangeTalkTarget(CObject* target) {
    // Current L2Server.exe 0x00ba658c.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(*m_lock);
        ReleaseEconomy();
    }
    CCreature::ChangeTalkTarget(target);
}

void User::ChangeTarget(CObject* target, ChangeTargetReason reason) {
    // Current L2Server 0x00ba94d0.
    L2_GUARD_FUNCTION();

    const CSmartID previousTarget = m_spTarget;
    if (!previousTarget.IsValid()) {
        m_nOldTargetId = 0;
    } else if (reason == CTR_BY_USER && m_nOldTargetId == static_cast<std::int32_t>(previousTarget.Sid())) {
        m_nOldTargetId = 0;
    } else if (reason == CTR_BY_USER || m_nOldTargetId != static_cast<std::int32_t>(previousTarget.Sid())) {
        m_nOldTargetId = static_cast<std::int32_t>(previousTarget.Sid());
    }

    CCreature::ChangeTarget(target, reason);

    if (target != nullptr) {
        if (target->IsNPC() && target->CastNPC()->m_data->m_nClassID == 0x54) {
            return;
        }

        if (target->IsCreature()) {
            CCreature* const targetCreature = target->CastCreature();
            const int hp = targetCreature == this ? runtime::Win64CvttDoubleToInt32(targetCreature->m_data->m_dHP)
                                                  : targetCreature->GetHPToShow();

            if (g_bSendSummonHPMPInfo || !target->IsSummon() || IsMyParty(static_cast<CCreature*>(target))) {
                GetSocket()->Send(
                    "cddcccdcd", 0x18, static_cast<int>(target->m_nId), static_cast<int>(m_nId), 0, 2, 10,
                    runtime::Win64CvttDoubleToInt32(targetCreature->m_data->m_dBaseHP), 9, hp
                );
            }

            if (target != this) {
                int carrierId = CContainerDB::Inst().GetVehicleID(targetCreature->m_data->container_index);
                const int targetAirShipId = static_cast<int>(target->GetMyAirShipID());
                const int actorShuttleId = static_cast<int>(GetMyShuttleID());
                if (carrierId < 1 && targetAirShipId >= 1) {
                    carrierId = targetAirShipId;
                }

                if (carrierId >= 1) {
                    const FVector position = targetCreature->m_data->m_Pos;
                    GetSocket()->Send(
                        "cdddddd", 0x80, static_cast<int>(target->m_nId), carrierId,
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z),
                        static_cast<int>(static_cast<std::uint16_t>(target->GetDir().Yaw))
                    );
                } else if (actorShuttleId >= 1) {
                    const FVector position = targetCreature->m_data->m_Pos;
                    GetSocket()->Send(
                        "chdddddd", 0xfe, 0xd1, static_cast<int>(target->m_nId), actorShuttleId,
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z),
                        static_cast<int>(static_cast<std::uint16_t>(target->GetDir().Yaw))
                    );
                } else {
                    // Current source evaluates the three virtual GetPos
                    // calls in z/y/x order instead of retaining one value.
                    const int z = runtime::Win64CvttDoubleToInt32(targetCreature->GetPos().z);
                    const int y = runtime::Win64CvttDoubleToInt32(targetCreature->GetPos().y);
                    const int x = runtime::Win64CvttDoubleToInt32(targetCreature->GetPos().x);
                    GetSocket()->Send(
                        "cdddddc", 0x79, static_cast<int>(target->m_nId), x, y, z,
                        static_cast<int>(static_cast<std::uint16_t>(targetCreature->GetDir().Yaw)), -1
                    );
                }
            }

            targetCreature->AddUserTargetsMe(*this);
        }
    }

    CCreature* const oldTarget = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nOldTargetId));
    if (oldTarget != nullptr) {
        oldTarget->DelUserTargetsMe(*this);
    }
}

void User::DeleteEnchantOptionSkills(CItem& item) {
    // Current L2Server 0x00bac70c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    std::map<int, const CSkillInfo*> acquiredSkills;
    std::map<int, const CSkillInfo*> effectSkills;
    ItemOptionHandler::GetEnchantOptionSkills(item, acquiredSkills, effectSkills);

    for (const auto& skill : acquiredSkills) {
        DeleteAcquireSkill(skill.second->m_nSkillId);
    }

    L2_GUARD(m_OptionSkillSect);
    auto skill = m_OptionSkills.begin();
    while (skill != m_OptionSkills.end()) {
        if (effectSkills.find((*skill)->m_nSkillId) == effectSkills.end()) {
            ++skill;
        } else {
            skill = m_OptionSkills.erase(skill);
        }
    }
}

void User::GetSkillsFromItem(
    CItem& item, std::map<int, const CSkillInfo*>& skills, const CheckCrystalType checkCrystalType
) {
    // Current L2Server 0x00bae7d4.
    L2_GUARD_FUNCTION();
    if (!IsSkillAcquireableItem(item, checkCrystalType) || item.m_data == nullptr || item.m_const == nullptr) {
        return;
    }

    CCursedWeaponMgr& cursedWeaponManager = CCursedWeaponMgr::inst();
    const bool cursedWeapon =
        cursedWeaponManager.FindConst(item.m_data->m_nClassID) != nullptr && m_nCursedWeaponClassId > 0;

    for (const CSkillInfo* skillInfo : item.m_ItemSkillList) {
        if (skillInfo == nullptr) {
            continue;
        }

        const CSkillInfo* selectedSkill = skillInfo;
        if (cursedWeapon) {
            int level = skillInfo->m_nLevel;
            if (skillInfo->m_OperateType == SKILL_PASSIVE || skillInfo->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE) {
                DeleteAcquireSkill(skillInfo->m_nSkillId);
                level = cursedWeaponManager.GetWeaponLevel(item.m_data->m_nClassID);
            }
            selectedSkill = g_SkillDB.GetSkillInfo(CSkillKey(skillInfo->m_nSkillId, level, 0), this);
        }

        if (selectedSkill == nullptr) {
            continue;
        }
        const auto current = skills.find(selectedSkill->m_nSkillId);
        if (current == skills.end() || current->second == nullptr || current->second->m_nLevel < selectedSkill->m_nLevel) {
            skills[selectedSkill->m_nSkillId] = selectedSkill;
        }
    }

    ItemOptionHandler::GetAcquiredSkills(item, skills);
    item.GetEnsoulSkills(skills);

    const CSkillInfo* const enchantedFourSkill = static_cast<const CSkillInfo*>(item.m_const->m_pEnchantedFourSkill);
    if (enchantedFourSkill != nullptr && item.m_data->GetEnchanted() > 3) {
        skills[enchantedFourSkill->m_nSkillId] = enchantedFourSkill;
    }
}

void User::_OnEquipItem(CItem* item, SlotType slotType) {
    // Current L2Server 0x00baea24, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }

    std::map<int, const CSkillInfo*> skills;
    GetSkillsFromItem(*item, skills, CCT_WITHIN_CRYSTAL_LEVEL);

    const std::uint32_t equipReuseDelay =
        item->m_const->m_nEquipReuseDelay > 0 ? static_cast<std::uint32_t>(item->m_const->m_nEquipReuseDelay) * 1000U : 0U;
    const bool hasVariation =
        item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0;

    for (const auto& entry : skills) {
        const CSkillInfo* const skillInfo = entry.second;
        if (skillInfo == nullptr) {
            continue;
        }
        if (slotType >= ST_AGATHION_SUB1 && slotType <= ST_AGATHION_SUB4 && skillInfo->m_bIsAgathionMainSkill) {
            continue;
        }
        if (!AcquireSkillByItem(skillInfo) || !skillInfo->IsActiveSkill()) {
            continue;
        }

        const std::uint32_t variationReuseDelay = hasVariation && skillInfo->m_bIsVariationItemEquipReusedelay
            ? static_cast<std::uint32_t>(skillInfo->GetNormalReuseDelay(this))
            : 0U;
        const std::uint32_t reuseDelay = equipReuseDelay + variationReuseDelay;
        if (reuseDelay == 0U) {
            continue;
        }

        const std::uint32_t reuseEnd =
            m_SkillReuseDelay.GetSkillUsedTime(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
        if (reuseEnd < runtime::GetTickCount() + reuseDelay) {
            m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(skillInfo->m_nSkillId, 4, reuseDelay, 0);
        }
    }

    item->ActivateEquipSkillList(this, this);
    if (item->m_data->m_Type == IT_WEAPON) {
        m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(m_IsSheathingWeapon, false, 0x5a);
    }
    if (slotType >= ST_ARTIFACT_A1 && slotType <= ST_ARTIFACT_D3) {
        runtime::raw_atomic::ExchangeI32(&m_ArtifactSlotChanged, 1);
    }
}

void User::_OnUnequipItem(CItem* item, SlotType slotType) {
    // Current L2Server 0x00ba733c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (item != nullptr) {
        item->ActivateUnequipSkillList(this, this);
        _OnUnequipItemProcess(item, slotType);
    }
}

void User::_OnUnequipItemProcess(CItem* item, SlotType slotType) {
    // Current L2Server 0x00baed2c, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }

    std::map<int, const CSkillInfo*> skills;
    GetSkillsFromItem(*item, skills, CCT_ANY_CRYSTAL_LEVEL);
    for (const auto& entry : skills) {
        const CSkillInfo* const skillInfo = entry.second;
        if (skillInfo == nullptr) {
            continue;
        }
        if (slotType >= ST_AGATHION_SUB1 && slotType <= ST_AGATHION_SUB4 && skillInfo->m_bIsAgathionMainSkill) {
            continue;
        }
        DeleteAcquireSkill(skillInfo->m_nSkillId);
    }
    ActivateEquipSkills();

    if (item->IsDuration()) {
        item->SetDurationCommitTick(0);
        _DelEquipDurationItem(item);

        const FVector position = GetPos();
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameStorage{};
        const wchar_t* const accountName = UserSkillAccountName(m_sAccountName, accountNameStorage);
#endif
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,,,%I64d",
        //     0x3d5,
        //     GetDBID(),
        //     GetAccountID(),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     item->Id(),
        //     item->GetDuration(),
        //     GetName(),
        //     accountName,
        //     item->GetDBID())
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,,,%I64d",
            0x3d5, GetDBID(), GetAccountID(), runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), item->Id(),
            item->GetDuration(), GetName(), accountName, item->GetDBID()
        );
    }

    if (slotType >= ST_ARTIFACT_A1 && slotType <= ST_ARTIFACT_D3) {
        runtime::raw_atomic::ExchangeI32(&m_ArtifactSlotChanged, 1);
    }
}

void User::ValidateEquippedAgathionSubSlotItemSkills(
    CItem& item,
    std::map<int, const CSkillInfo*>& withinSkills,
    std::map<int, const CSkillInfo*>& beyondSkills,
    std::map<int, const CSkillInfo*>& optionSkills
) {
    // Current L2Server 0x00baf008.
    L2_GUARD_FUNCTION();

    std::map<int, const CSkillInfo*> itemWithinSkills;
    GetSkillsFromItem(item, itemWithinSkills, CCT_WITHIN_CRYSTAL_LEVEL);
    for (const auto& entry : itemWithinSkills) {
        const CSkillInfo* const skillInfo = entry.second;
        if (skillInfo == nullptr || skillInfo->m_bIsAgathionMainSkill) {
            continue;
        }
        const auto current = withinSkills.find(skillInfo->m_nSkillId);
        if (current == withinSkills.end() || current->second == nullptr || current->second->m_nLevel < skillInfo->m_nLevel) {
            withinSkills[skillInfo->m_nSkillId] = skillInfo;
        }
    }

    std::map<int, const CSkillInfo*> itemBeyondSkills;
    GetSkillsFromItem(item, itemBeyondSkills, CCT_BEYOND_CRYSTAL_LEVEL);
    for (const auto& entry : itemBeyondSkills) {
        const CSkillInfo* const skillInfo = entry.second;
        if (skillInfo == nullptr || skillInfo->m_bIsAgathionMainSkill) {
            continue;
        }
        const auto current = beyondSkills.find(skillInfo->m_nSkillId);
        if (current == beyondSkills.end() || current->second == nullptr || current->second->m_nLevel < skillInfo->m_nLevel) {
            beyondSkills[skillInfo->m_nSkillId] = skillInfo;
        }
    }

    const CSkillInfo* const enchantedFourSkill = static_cast<const CSkillInfo*>(item.m_const->m_pEnchantedFourSkill);
    if (enchantedFourSkill != nullptr && item.m_data->GetEnchanted() < 4) {
        beyondSkills[enchantedFourSkill->m_nSkillId] = enchantedFourSkill;
    }
    GetOptionSkillsFromItem(item, optionSkills, CCT_WITHIN_CRYSTAL_LEVEL);
}

void User::UpdateArtifactbookPageSkills() {
    // Current L2Server 0x00baf8b4.
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::CompareExchangeI32(&m_ArtifactSlotChanged, 0, 1) != 1) {
        return;
    }

    constexpr std::array<std::array<SlotType, 7>, 3> artifactSlotGroups{{
        {{
            ST_ARTIFACT_A1,
            ST_ARTIFACT_A2,
            ST_ARTIFACT_A3,
            ST_ARTIFACT_A4,
            ST_ARTIFACT_B1,
            ST_ARTIFACT_C1,
            ST_ARTIFACT_D1,
        }},
        {{
            ST_ARTIFACT_A5,
            ST_ARTIFACT_A6,
            ST_ARTIFACT_A7,
            ST_ARTIFACT_A8,
            ST_ARTIFACT_B2,
            ST_ARTIFACT_C2,
            ST_ARTIFACT_D2,
        }},
        {{
            ST_ARTIFACT_A9,
            ST_ARTIFACT_A10,
            ST_ARTIFACT_A11,
            ST_ARTIFACT_A12,
            ST_ARTIFACT_B3,
            ST_ARTIFACT_C3,
            ST_ARTIFACT_D3,
        }},
    }};

    int fullGroupCount = 0;
    for (const auto& group : artifactSlotGroups) {
        int occupiedSlotCount = 0;
        for (const SlotType slot : group) {
            if (m_data->m_nSlotIndex[static_cast<int>(slot)] == -1) {
                break;
            }
            ++occupiedSlotCount;
        }
        if (occupiedSlotCount == static_cast<int>(group.size())) {
            ++fullGroupCount;
        }
    }

    CArtifactDB& artifactDatabase = Singleton<CArtifactDB>::inst();
    const std::vector<const CSkillInfo*>* pageSkills =
        artifactDatabase.GetArtifactbookPageSkills(m_nFullArtifactSlotGroupCount);
    if (m_nFullArtifactSlotGroupCount == fullGroupCount) {
        if (pageSkills != nullptr) {
            for (const CSkillInfo* const skillInfo : *pageSkills) {
                if (!HaveSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel)) {
                    AcquireSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, false, true);
                }
            }
        }
        return;
    }

    if (pageSkills != nullptr) {
        for (const CSkillInfo* const skillInfo : *pageSkills) {
            DeleteAcquireSkill(skillInfo->m_nSkillId);
        }
    }
    pageSkills = artifactDatabase.GetArtifactbookPageSkills(fullGroupCount);
    if (pageSkills != nullptr) {
        for (const CSkillInfo* const skillInfo : *pageSkills) {
            AcquireSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, false, true);
        }
    }
    m_nFullArtifactSlotGroupCount = fullGroupCount;
}

bool User::ValidateItemSkills() {
    // Current L2Server 0x00bafb00.
    L2_GUARD_FUNCTION();

    std::map<int, const CSkillInfo*> withinSkills;
    std::map<int, const CSkillInfo*> beyondSkills;
    std::map<int, const CSkillInfo*> optionSkills;
    std::map<int, const CSkillInfo*> transferSkills;
    std::map<int, std::pair<SlotType, CItem*>> equippedItems;

    for (int slot = 0; slot < ST_MAX; ++slot) {
        CItem* const item = CItemFactory::Inst().GetObjectW(m_data->m_nSlotIndex[slot]);
        if (item != nullptr) {
            equippedItems.emplace(runtime::Int32FromBits(item->m_nId), std::make_pair(static_cast<SlotType>(slot), item));
        }
    }

    for (const auto& entry : equippedItems) {
        const SlotType slot = entry.second.first;
        CItem* const item = entry.second.second;
        if (slot >= ST_MAX || item == nullptr) {
            continue;
        }
        if (slot >= ST_AGATHION_SUB1 && slot <= ST_AGATHION_SUB4) {
            ValidateEquippedAgathionSubSlotItemSkills(*item, withinSkills, beyondSkills, optionSkills);
            continue;
        }

        GetSkillsFromItem(*item, withinSkills, CCT_WITHIN_CRYSTAL_LEVEL);
        GetSkillsFromItem(*item, beyondSkills, CCT_BEYOND_CRYSTAL_LEVEL);
        const CSkillInfo* const enchantedFourSkill = static_cast<const CSkillInfo*>(item->m_const->m_pEnchantedFourSkill);
        if (enchantedFourSkill != nullptr && item->m_data->GetEnchanted() < 4) {
            beyondSkills[enchantedFourSkill->m_nSkillId] = enchantedFourSkill;
        }
        GetOptionSkillsFromItem(*item, optionSkills, CCT_WITHIN_CRYSTAL_LEVEL);
    }

    bool changed = false;
    for (const auto& entry : beyondSkills) {
        const bool deleted = DeleteAcquireSkill(entry.second->m_nSkillId);
        changed = changed || deleted;
    }
    for (const auto& entry : withinSkills) {
        const CSkillInfo* const skillInfo = entry.second;
        const bool acquired = AcquireSkillByItem(skillInfo);
        changed = changed || acquired;

        if (m_SkillMod.m_bItemSkillTransfer &&
            g_ItemSkillTransfer.find(static_cast<std::int64_t>(skillInfo->m_nSkillId)) != g_ItemSkillTransfer.end()) {
            transferSkills[skillInfo->m_nSkillId] = skillInfo;
        }
    }

    {
        L2_GUARD(m_OptionSkillSect);
        m_OptionSkills.clear();
        for (const auto& entry : optionSkills) {
            m_OptionSkills.push_back(entry.second);
        }
    }

    UpdateArtifactbookPageSkills();

    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            this, summon,
            // Recovered source used:
            // if (!summon->IsActive()) { continue. }
            // Current 0x00bafb00 calls vslot +0x448 (IsAlive), so an alive
            // but inactive summon still participates in item-skill transfer.
            if (!summon->IsAlive()) { continue; }

            L2_GUARD(summon->m_TransferItemSkillSect);
            bool summonChanged = false; for (const CSkillInfo* const skillInfo : summon->m_TransferItemSkills) {
                if (transferSkills.find(skillInfo->m_nSkillId) == transferSkills.end()) {
                    const bool deleted = summon->DeleteAcquireSkill(skillInfo->m_nSkillId);
                    summonChanged = summonChanged || deleted;
                }
            } summon->m_TransferItemSkills.clear();

            for (const auto& entry : transferSkills) {
                const CSkillInfo* const skillInfo = entry.second;
                summon->m_TransferItemSkills.push_back(skillInfo);
                const bool acquired =
                    summon->AcquireSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel, false, false);
                summonChanged = summonChanged || acquired;
            } if (summonChanged) { summon->ValidateChanged(VB_MAX); }
        );
    }

    return changed;
}

void User::ActivateEquipSkills() {
    L2_GUARD_FUNCTION();
    for (const int itemIndex : m_data->m_nSlotIndex) {
        CItem* const item = CItemFactory::Inst().GetObjectW(itemIndex);
        if (item != nullptr) {
            item->ActivateEquipSkillList(this, this);
        }
    }
}

void User::PumpStartTransformSkill() {
    L2_GUARD_FUNCTION();
    if (m_transform == nullptr) {
        return;
    }

    std::vector<const CSkillInfo*> skills;
    m_transform->GetSkillList(m_data->m_nLevel, skills);
    for (const CSkillInfo* const skillInfo : skills) {
        if (skillInfo != nullptr &&
            (skillInfo->m_OperateType == SKILL_PASSIVE || skillInfo->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE)) {
            PushSerializedMessage(
                new CSerializedOnInfectedSkillByMessage(this, this, *skillInfo, SPST_ACQUIRE_SKILL, false)
            );
        }
    }
}

void User::PumpEndTransformSkill() {
    L2_GUARD_FUNCTION();
    if (m_transform == nullptr) {
        return;
    }

    std::vector<const CSkillInfo*> skills;
    m_transform->GetSkillList(m_data->m_nLevel, skills);
    for (const CSkillInfo* const skillInfo : skills) {
        if (skillInfo != nullptr &&
            (skillInfo->m_OperateType == SKILL_PASSIVE || skillInfo->m_OperateType == SKILL_RACE_SPECIAL_PASSIVE)) {
            PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *skillInfo, SPET_DELETE_SKILL));
        }
    }
}

void User::_GetTransformSkillListByType(
    const CSkillInfo* const skillInfo,
    std::vector<const CSkillInfo*>& activeSkills,
    std::vector<const CSkillInfo*>& passiveSkills,
    std::vector<const CSkillInfo*>& toggleSkills,
    std::vector<const CSkillInfo*>& auraSkills
) {
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return;
    }

    const SkillOperateType operateType = skillInfo->m_OperateType;
    std::vector<const CSkillInfo*>* destination = nullptr;
    if (operateType == SKILL_PASSIVE || operateType == SKILL_RACE_SPECIAL_PASSIVE) {
        if (skillInfo->m_nSkillId == 14571 || skillInfo->m_nSkillId == 23344) {
            m_data->m_nDeathPenaltyLevel = skillInfo->m_nLevel;
        }
        for (auto current = passiveSkills.begin(); current != passiveSkills.end(); ++current) {
            if ((*current)->m_nSkillId == skillInfo->m_nSkillId) {
                if (skillInfo->m_nLevel <= (*current)->m_nLevel) {
                    return;
                }
                passiveSkills.erase(current);
                break;
            }
        }
        destination = &passiveSkills;
    } else {
        if (m_transform == nullptr ||
            ((static_cast<std::uint32_t>(skillInfo->m_nTransformType) >>
                 (static_cast<std::uint32_t>(m_transform->m_eTransformType) & 31U)) &
                1U) == 0U) {
            return;
        }
        if (operateType == SKILL_TOGGLE || operateType == SKILL_GROUP_TOGGLE) {
            destination = &toggleSkills;
        } else if (operateType == SKILL_AURA) {
            destination = &auraSkills;
        } else {
            destination = &activeSkills;
        }
    }
    destination->push_back(skillInfo);
}

void User::ValidateSkillListInTransform() {
    L2_GUARD_FUNCTION();
    if (m_transform == nullptr) {
        return;
    }

    std::vector<const CSkillInfo*> activeSkills;
    std::vector<const CSkillInfo*> passiveSkills;
    std::vector<const CSkillInfo*> toggleSkills;
    std::vector<const CSkillInfo*> auraSkills;
    std::vector<const CSkillInfo*> transformSkills;
    m_transform->GetSkillList(m_data->m_nLevel, transformSkills);
    for (const CSkillInfo* const skillInfo : transformSkills) {
        _GetSkillListByType(skillInfo, activeSkills, passiveSkills, toggleSkills, auraSkills);
    }

    {
        L2_GUARD(m_SkillListSect);
        for (const CSkillInfo* const transformPassive : passiveSkills) {
            for (const CSkillInfo* const currentPassive : m_PassiveSkills) {
                if (currentPassive->m_nSkillId == transformPassive->m_nSkillId &&
                    currentPassive->m_nLevel < transformPassive->m_nLevel) {
                    PushSerializedMessage(new CSerializedPumpEndMessage(this, this, *currentPassive, SPET_DELETE_SKILL));
                    PushSerializedMessage(
                        new CSerializedOnInfectedSkillByMessage(this, this, *transformPassive, SPST_ACQUIRE_SKILL, false)
                    );
                    break;
                }
            }
        }
    }

    {
        L2_GUARD(m_AcquiredSkillsSect);
        m_data->m_nDeathPenaltyLevel = 0;
        for (const auto& acquiredSkill : m_AcquiredSkills) {
            const AcquiredSkillInfo& info = acquiredSkill.second;
            const CSkillKey key(info.m_nId, info.m_nLevel, info.m_nSubLevel);
            const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(key, this);
            _GetTransformSkillListByType(skillInfo, activeSkills, passiveSkills, toggleSkills, auraSkills);
        }
    }

    ResetSkillsAfterValidateSkillForTransform(activeSkills, passiveSkills, toggleSkills, auraSkills);
}

void User::ValidateSkillList() {
    // Current L2Server 0x00bae6d0, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    if (IsTransformed(TransformType::Max) == nullptr) {
        CCreature::ValidateSkillList();
        SendSkillList(nullptr, false, 0);
        return;
    }

    const bool includePreviousSkills = m_data->m_NeedUpdatePacketBitSet.IsSet(5);
    ValidateSkillListInTransform();
    SendSkillList(nullptr, includePreviousSkills, 0);
}

CObject* User::SetSkillTarget(CObject* target) {
    // Current L2Server 0x00ba7088, original owner user_skill.cpp.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_pSkillTargetObject = target;
        if (target == nullptr) {
            m_spTarget = nullptr;
        } else {
            m_spTarget = target->GetSmartID();
        }
    }
    return m_pSkillTargetObject;
}

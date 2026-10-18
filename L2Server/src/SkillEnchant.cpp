#include "SkillEnchant.h"
#include "AtomicJobServerSide.h"
#include "CreatureController.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "ParserSkillEnchantData.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Skill.h"
#include "TransformConst.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>

namespace {

std::uint32_t PackSkillLevel(int level, int subLevel) {
    return (static_cast<std::uint32_t>(subLevel) << 16) | static_cast<std::uint16_t>(level);
}

std::int64_t PackSkillNameId(int skillId, int level, int subLevel) {
    // Current SendEnchantSkillInfo@0x00a6d99c, instructions
    // 0x00a6da2c..0x00a6da47, build the internal name key as
    // skill-id:level:sublevel. This is not the client packet's
    // sublevel:level layout produced by PackSkillLevel.
    const std::uint64_t packed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(skillId)) << 32) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(level)) << 16) | static_cast<std::uint16_t>(subLevel);
    return static_cast<std::int64_t>(packed);
}

L2_DEFINE_FACTORY_CREATOR(SkillEnchantOperator, CreateSkillEnchantNormal, SkillEnchantNormal)
L2_DEFINE_FACTORY_CREATOR(SkillEnchantOperator, CreateSkillEnchantSafe, SkillEnchantSafe)
L2_DEFINE_FACTORY_CREATOR(SkillEnchantOperator, CreateSkillEnchantUntrain, SkillEnchantUntrain)
L2_DEFINE_FACTORY_CREATOR(SkillEnchantOperator, CreateSkillEnchantRouteChange, SkillEnchantRouteChange)
L2_DEFINE_FACTORY_CREATOR(SkillEnchantOperator, CreateSkillEnchantForce, SkillEnchantForce)

L2_REGISTER_FACTORY_CREATOR(kRegisterSkillEnchantNormal, SkillEnchantFactory, SECT_NORMAL, &CreateSkillEnchantNormal);
L2_REGISTER_FACTORY_CREATOR(kRegisterSkillEnchantSafe, SkillEnchantFactory, SECT_SAFE, &CreateSkillEnchantSafe);
L2_REGISTER_FACTORY_CREATOR(kRegisterSkillEnchantUntrain, SkillEnchantFactory, SECT_UNTRAIN, &CreateSkillEnchantUntrain);
L2_REGISTER_FACTORY_CREATOR(
    kRegisterSkillEnchantRouteChange, SkillEnchantFactory, SECT_ROUTE_CHANGE, &CreateSkillEnchantRouteChange
);
L2_REGISTER_FACTORY_CREATOR(kRegisterSkillEnchantForce, SkillEnchantFactory, SECT_FORCE, &CreateSkillEnchantForce);

} // namespace

SkillEnchantOperator::SkillEnchantOperator() : m_eType(SECT_MAX) {}

SkillEnchantNormal::SkillEnchantNormal() { m_eType = SECT_NORMAL; }

SkillEnchantSafe::SkillEnchantSafe() { m_eType = SECT_SAFE; }

SkillEnchantUntrain::SkillEnchantUntrain() { m_eType = SECT_UNTRAIN; }

SkillEnchantRouteChange::SkillEnchantRouteChange() { m_eType = SECT_ROUTE_CHANGE; }

SkillEnchantForce::SkillEnchantForce() { m_eType = SECT_FORCE; }

void SkillEnchantOperator::BuilderEnchantSkillReset(User* user, CUserSocket* socket) {
    // Current L2Server 0x00a6d774.
    L2_GUARD_FUNCTION();
    CObject* const selected = user->m_spTarget.GetObjectW();
    if (selected != nullptr) {
        if (!selected->IsPC()) {
            socket->SendSystemMessage(u"SYS", u"invalid target");
            return;
        }
        user = selected->CastUser();
    }

    if (user == nullptr) {
        socket->SendSystemMessage(u"SYS", u"invalid target");
        return;
    }

    user->ResetAllEnchantSkill(socket);
    user->m_ShortCut.SendShortCutInfo();
}

SkillEnchantCommandType SkillEnchantOperator::GetSkillEnchantTypeByCmd(const char16_t* command) {
    // Current L2Server 0x00a6d898.
    L2_GUARD_FUNCTION();
    if (target_utf16::CompareN(command, u"_safe", 5) == 0) {
        return SECT_SAFE;
    }
    if (target_utf16::CompareN(command, u"_untrain", 8) == 0) {
        return SECT_UNTRAIN;
    }
    if (target_utf16::CompareN(command, u"_route_change", 13) == 0) {
        return SECT_ROUTE_CHANGE;
    }
    return SECT_NORMAL;
}

void SkillEnchantOperator::SendEnchantSkillInfo(
    User* const user, CUserSocket*, const int skillId, const int skillLevelAndSubLevel, const bool detail
) {
    // Current L2Server 0x00a6d99c.
    L2_GUARD_FUNCTION();
    const std::int64_t skillNameId =
        PackSkillNameId(skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16);
    if (detail) {
        SendAcquirableSkillDetail(user, skillNameId);
    } else {
        SendAcquirableSkillList(user, skillNameId);
    }
}

void SkillEnchantOperator::SendAcquiredSkillList(User*) {
    // Current L2Server folded no-op virtual at 0x006fe5d4.
    L2_GUARD_FUNCTION();
}

bool SkillEnchantOperator::CheckRequestSkill(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00a6da80.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    const int acquiredLevel = user->GetAcquiredSkillLevel(skillId);
    if (acquiredLevel == -1 || acquiredLevel != skillLevel || user->HaveSkill(skillId, skillLevel, skillSubLevel)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_UNLEARNED_SKILL);
        return false;
    }
    if (!user->IsThirdClassChangedOrMore()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_THIRD_CLASS_TRANSFER);
        return false;
    }
    if (user->Level() <= 75) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_LEVEL_76_OR_HIGHER);
        return false;
    }
    return true;
}

bool SkillEnchantOperator::CheckRequestEnchantSkill(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel, SkillEnchantInfo*& info
) {
    // Current L2Server 0x00a70554.
    L2_GUARD_FUNCTION();
    if (user == nullptr || !CheckRequestSkill(user, skillId, skillLevel, skillSubLevel)) {
        return false;
    }

    info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hacking? user[%s] skill_id[%d] skill_level[%d] skill sublev[%d]",
        //     L"SkillEnchant.cpp",
        //     0xbcf,
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hacking? user[%s] skill_id[%d] skill_level[%d] skill sublev[%d]",
        //     L"SkillEnchant.cpp",
        //     0xbcf,
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     "Enchant request rejected for user '%s': no applicable enchant data for skill %d level %d sublevel %d.",
        //     WideToUtf8(user->GetName()).c_str(), skillId, skillLevel, skillSubLevel)
        L2_LOG_ERROR(
            Log, "Enchant request rejected for user '%s': no applicable enchant data for skill %d level %d sublevel %d.",
            Utf16ToUtf8(user->GetName()).c_str(), skillId, skillLevel, skillSubLevel
        );
        return false;
    }

    const int acquiredLevel = user->GetAcquiredSkillLevel(skillId);
    const int acquiredSubLevel = user->GetAcquiredSkillSubLevel(skillId);
    if (acquiredLevel != skillLevel || acquiredSubLevel != info->nNeedSkillSubLevel) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_UNLEARNED_SKILL);
        return false;
    }

    if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), user) != nullptr) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Skill [%d][%d], Failed to get skillinfo",
    //     L"SkillEnchant.cpp",
    //     0xbe0,
    //     skillId,
    //     skillSubLevel)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid Skill [%d][%d], Failed to get skillinfo",
    //     L"SkillEnchant.cpp",
    //     0xbe0,
    //     skillId,
    //     skillSubLevel)
    L2_LOG_ERROR(
        Log, "Enchant request rejected: skill %d level %d sublevel %d has no available skill definition.", skillId,
        skillLevel, skillSubLevel
    );
    return false;
}

bool SkillEnchantRouteChange::CheckRequestEnchantSkill(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel, SkillEnchantInfo*& info
) {
    // Current L2Server 0x00a70734.
    L2_GUARD_FUNCTION();
    if (user == nullptr || !CheckRequestSkill(user, skillId, skillLevel, skillSubLevel)) {
        return false;
    }

    info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hacking? user[%s] skill_id[%d] skill_level[%d] skill sublev[%d]",
        //     L"SkillEnchant.cpp",
        //     0xbff,
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hacking? user[%s] skill_id[%d] skill_level[%d] skill sublev[%d]",
        //     L"SkillEnchant.cpp",
        //     0xbff,
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     "Enchant route-change request rejected for user '%s': no applicable enchant data for skill %d level %d
        //     sublevel %d.", WideToUtf8(user->GetName()).c_str(), skillId, skillLevel, skillSubLevel)
        L2_LOG_ERROR(
            Log,
            "Enchant route-change request rejected for user '%s': no applicable enchant data for skill %d level %d sublevel "
            "%d.",
            Utf16ToUtf8(user->GetName()).c_str(), skillId, skillLevel, skillSubLevel
        );
        return false;
    }

    const int acquiredLevel = user->GetAcquiredSkillLevel(skillId);
    const int acquiredSubLevel = user->GetAcquiredSkillSubLevel(skillId);
    if (acquiredLevel != skillLevel) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_UNLEARNED_SKILL);
        return false;
    }

    SkillEnchantInfo* const current = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, acquiredLevel, acquiredSubLevel);
    if (current == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"skill is not enchanted[%d][%d][%d]",
        //     skillId,
        //     acquiredLevel,
        //     acquiredSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"skill is not enchanted[%d][%d][%d]",
        //     skillId,
        //     acquiredLevel,
        //     acquiredSubLevel)
        L2_LOG_ERROR(
            Log,
            "Enchant route-change request rejected: no applicable enchant data for learned skill %d level %d sublevel %d.",
            skillId, acquiredLevel, acquiredSubLevel
        );
        return false;
    }
    if (current->nEnchantId != info->nEnchantId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"skill enchant depth is diff[%d][%d][%d] vs [%d][%d][%d]",
        //     skillId,
        //     acquiredLevel,
        //     acquiredSubLevel,
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"skill enchant depth is diff[%d][%d][%d] vs [%d][%d][%d]",
        //     skillId,
        //     acquiredLevel,
        //     acquiredSubLevel,
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        L2_LOG_ERROR(
            Log,
            "Enchant route-change request rejected for skill %d: current level %d sublevel %d and requested level %d "
            "sublevel %d have different enchant steps.",
            skillId, acquiredLevel, acquiredSubLevel, skillLevel, skillSubLevel
        );
        return false;
    }
    if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), user) != nullptr) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid Skill [%d][%d], Failed to get skillinfo",
    //     L"SkillEnchant.cpp",
    //     0xc21,
    //     skillId,
    //     skillSubLevel)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid Skill [%d][%d], Failed to get skillinfo",
    //     L"SkillEnchant.cpp",
    //     0xc21,
    //     skillId,
    //     skillSubLevel)
    L2_LOG_ERROR(
        Log, "Enchant route-change request rejected: skill %d level %d sublevel %d has no available skill definition.",
        skillId, skillLevel, skillSubLevel
    );
    return false;
}

bool SkillEnchantUntrain::CheckRequestEnchantSkill(User*, int, int, int, SkillEnchantInfo*&) {
    // Current L2Server 0x00a6de44 accepts every request.
    L2_GUARD_FUNCTION();
    return true;
}

bool SkillEnchantOperator::CheckEnchantSkill(User* const user, const std::int64_t skillNameId, SkillEnchantInfo*& info) {
    // Current L2Server 0x00a6f930.
    L2_GUARD_FUNCTION();
    if (!user->IsThirdClassChangedOrMore()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_THIRD_CLASS_TRANSFER);
        return false;
    }
    if (user->Level() <= 75) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_LEVEL_76_OR_HIGHER);
        return false;
    }

    const std::uint64_t packed = static_cast<std::uint64_t>(skillNameId);
    const int skillId = static_cast<int>(packed >> 32);
    const int skillLevel = static_cast<std::uint16_t>(packed >> 16);
    const int skillSubLevel = static_cast<std::uint16_t>(packed);
    if (g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), user) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid skill_id[%d] skill_level[%d]",
        //     L"SkillEnchant.cpp",
        //     0x4f5,
        //     skillId,
        //     skillLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid skill_id[%d] skill_level[%d]",
        //     L"SkillEnchant.cpp",
        //     0x4f5,
        //     skillId,
        //     skillLevel)
        L2_LOG_ERROR(
            Log, "Cannot inspect enchantment for skill %d level %d sublevel %d: no available skill definition.", skillId,
            skillLevel, skillSubLevel
        );
        return false;
    }

    info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info != nullptr) {
        return true;
    }

    const auto acquired = user->m_AcquiredSkills.find(skillId);
    if (acquired == user->m_AcquiredSkills.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid original skill [%d:%d]",
        //     L"SkillEnchant.cpp",
        //     0x501,
        //     skillId,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid original skill [%d:%d]",
        //     L"SkillEnchant.cpp",
        //     0x501,
        //     skillId,
        //     skillSubLevel)
        L2_LOG_ERROR(
            Log,
            "Cannot inspect enchantment for skill %d level %d sublevel %d: no applicable enchant data and the user has not "
            "learned this skill.",
            skillId, skillLevel, skillSubLevel
        );
        return false;
    }
    info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, acquired->second.m_nLevel, acquired->second.m_nSubLevel);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid skill [%d:%d]",
        //     L"SkillEnchant.cpp",
        //     0x508,
        //     skillId,
        //     skillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid skill [%d:%d]",
        //     L"SkillEnchant.cpp",
        //     0x508,
        //     skillId,
        //     skillSubLevel)
        L2_LOG_ERROR(
            Log, "Cannot inspect enchantment for skill %d: no applicable enchant data for the learned level %d sublevel %d.",
            skillId, acquired->second.m_nLevel, acquired->second.m_nSubLevel
        );
        return false;
    }
    return true;
}

bool SkillEnchantOperator::CheckLearnable(
    CUserSocket* const socket, const SkillEnchantInfo* const info, const std::int64_t sp, const int level
) {
    // Current L2Server 0x00a6e894.
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    if (user->m_nChangeSubjobFlag == 1 || user->GetController()->GetCurrentActionType() == ACT_SKILL) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (user->IsShopManaging() || user->IsShopOpen() || user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::SYSTEM_UNAVAILABLE_DURING_TRADE_STORE_OR_WORKSHOP_SETUP);
        return false;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_STATE_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (user->m_AreaMod.m_bIsInBattleField &&
        (!user->IsNoSiegeRelation() || user->IsInFortressSiege() || user->IsInAgitWar(false))) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_STATE_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (user->IsInDuel() || user->IsRidingWyvern() || user->IsInAirShip() ||
        static_cast<std::int32_t>(user->m_data->m_moveType) == CMT_FLY || user->IsInAirShip() || user->IsOlympiadPlaying() ||
        user->m_PledgeGameUser.IsPlaying()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_STATE_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (user->m_SkillMod.m_nBlockMoveCount != 0 || !user->m_data->m_bCanMove || !user->m_SkillMod.m_bCanAct ||
        !user->IsAlive()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_SYSTEM_UNAVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (user->m_AreaMod.m_bIsUnderWater) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_SYSTEM_UNAVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (user->IsTransformed(TransformType::Max) != nullptr || user->IsRiding()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_STATE_REQUIREMENTS_NOT_MET);
        return false;
    }
    if (!user->IsThirdClassChangedOrMore()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_THIRD_CLASS_TRANSFER);
        return false;
    }
    if (level <= 75) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_LEVEL_76_OR_HIGHER);
        return false;
    }

    const std::int64_t requiredSp = GetRequiredSP(info);
    if (sp < requiredSp) {
        socket->SendSystemMessage(SystemMessageId::NOT_ENOUGH_SP_TO_ENCHANT_SKILL);
        return false;
    }
    const int acquiredLevel = user->GetAcquiredSkillLevel(info->nSkillID);
    const int acquiredSubLevel = user->GetAcquiredSkillSubLevel(info->nSkillID);
    if (user->GetRemainReuseDelaySec(info->nSkillID, acquiredLevel, acquiredSubLevel) > 0.0) {
        socket->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_STATE_REQUIREMENTS_NOT_MET);
        return false;
    }
    return true;
}

bool SkillEnchantOperator::CheckNpcShowEnchantSkillList(
    const int npcIndex, const int userSid, const int userIndex, User*& user, CNPC*& npc
) {
    // Current L2Server 0x00a6f21c.
    L2_GUARD_FUNCTION();
    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(userIndex) != userSid) {
        return false;
    }

    CSPointer<CCreature> userCreature = creatures.FindObjectSP(userIndex);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"SkillEnchant.cpp",
        //     0x440,
        //     userIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"SkillEnchant.cpp",
        //     0x440,
        //     userIndex)
        L2_LOG_ERROR(
            Log, "NPC enchant-list request rejected: creature index %d does not refer to an available player.", userIndex
        );
        return false;
    }
    CSPointer<CCreature> npcCreature = creatures.FindObjectSP(npcIndex);
    if (!npcCreature || !npcCreature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"SkillEnchant.cpp",
        //     0x447,
        //     npcIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"SkillEnchant.cpp",
        //     0x447,
        //     npcIndex)
        L2_LOG_ERROR(
            Log,
            "NPC enchant-list request rejected for player index %d: creature index %d does not refer to an available NPC.",
            userIndex, npcIndex
        );
        return false;
    }

    user = userCreature->CastUser();
    npc = npcCreature->CastNPC();
    if (!user->IsThirdClassChangedOrMore() || user->Level() <= 75) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] modify script event 'ENCHANT_SKILL_REQUESTED'",
        //     L"SkillEnchant.cpp",
        //     0x451)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] modify script event 'ENCHANT_SKILL_REQUESTED'",
        //     L"SkillEnchant.cpp",
        //     0x451)
        L2_LOG_ERROR(
            Log,
            "NPC index %d requested an enchant list for ineligible player index %d. Check script event "
            "ENCHANT_SKILL_REQUESTED: third class transfer and level 76 or higher are required.",
            npcIndex, userIndex
        );
        return false;
    }
    return true;
}

std::int64_t SkillEnchantOperator::GetRequiredSP(const SkillEnchantInfo* const info) const {
    // Current L2Server 0x00a6dbcc.
    L2_GUARD_FUNCTION();
    return info != nullptr ? info->nSp[static_cast<int>(m_eType)] : 0;
}

double SkillEnchantOperator::GetEnchantProb(const SkillEnchantInfo* const info) const {
    // Current L2Server 0x00a6dc78.
    L2_GUARD_FUNCTION();
    return info != nullptr ? info->dProb[static_cast<int>(m_eType)] : 0.0;
}

std::int64_t SkillEnchantOperator::GetRequiredAdena(SkillEnchantInfo* const info) const {
    // Current L2Server 0x00a6f4e4.
    L2_GUARD_FUNCTION();
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[SkillEnchantOperator::GetRequiredAdena] enchant info is null")
        // L2_LOG_ERROR(Log,
        //     L"[SkillEnchantOperator::GetRequiredAdena] enchant info is null")
        L2_LOG_ERROR(Log, "Cannot calculate the adena cost of skill enchantment: enchant data is null; returning 0.");
        return 0;
    }
    const auto found = info->RequiredItem[static_cast<int>(m_eType)].find(0x39);
    return found != info->RequiredItem[static_cast<int>(m_eType)].end() ? static_cast<std::int64_t>(found->second) : 0;
}

int SkillEnchantOperator::CalcSubLevelDown(const int skillSubLevel) const {
    // Current L2Server 0x00a6dd28.
    L2_GUARD_FUNCTION();
    const double value = std::floor(static_cast<double>(skillSubLevel % 1000) / 10.0) * 10.0;
    if (skillSubLevel / 1000 == 0 || value == 0.0 || value <= 0.0) {
        return 0;
    }
    return static_cast<int>(static_cast<double>((skillSubLevel / 1000) * 1000) + value);
}

bool SkillEnchantOperator::IsEnchantSuccess(const SkillEnchantInfo* const info) const {
    // Current L2Server 0x00a71570.
    L2_GUARD_FUNCTION();
    return ::genrand64_real1() * 100.0 + 0.0 <= GetEnchantProb(info);
}

bool SkillEnchantOperator::DoOperation(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel, AtomicJob* const job
) {
    // Current L2Server 0x00a72b84.
    L2_GUARD_FUNCTION();
    if (user == nullptr || job == nullptr) {
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return false;
    }

    SkillEnchantInfo* info = nullptr;
    if (!CheckRequestEnchantSkill(user, skillId, skillLevel, skillSubLevel, info) ||
        !CheckLearnable(socket, info, user->m_data->m_nSP, user->Level())) {
        return false;
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 0;
    for (const auto& required : info->RequiredItem[static_cast<int>(m_eType)]) {
        if (g_ObjectDB.GetObjectW(required.first, false) == nullptr) {
            return false;
        }
        if (user->m_inv.GetItemAmount(required.first, 0, true) < required.second) {
            socket->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEMS_TO_ENCHANT_SKILL);
            return false;
        }
        if (required.second > 0) {
            requests[requestCount] =
                ItemManip(0, required.first, -static_cast<std::int64_t>(required.second), 0, 0, 0, true);
            ++requestCount;
        }
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    const User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_SKILL_ENCHANT, static_cast<int>(m_eType), requestCount, requests.data(), job, 0, packet, false,
        ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (user->_HandleItemManipulation(queue)) {
            return true;
        }
        job->Destroy();
        return false;
    }
    if (result == User::MRT_JOBMAP) {
        job->Destroy();
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return true;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillEnchant.cpp",
        //     0x9e5)
        // Noise: The enchant job was successfully queued behind an existing item operation. This is normal waiting, not an
        // error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"SkillEnchant.cpp",
        //     0x9e5)
        return true;
    }

    job->Destroy();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"SkillEnchant.cpp",
    //     0x9eb)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"SkillEnchant.cpp",
    //     0x9eb)
    L2_LOG_ERROR(
        Log,
        "Skill-enchant item queue returned unexpected result %d for skill %d level %d sublevel %d; the job was discarded.",
        static_cast<int>(result), skillId, skillLevel, skillSubLevel
    );
    return true;
}

void SkillEnchantOperator::SendAcquirableSkillDetail(User* const user, const std::int64_t skillNameId) {
    // Current L2Server 0x00a6fc10.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    SkillEnchantInfo* info = nullptr;
    if (!CheckEnchantSkill(user, skillNameId, info)) {
        return;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_SKILLS_BEFORE_AWAKENED_CLASS_DIVERSIFICATION);
        return;
    }
    if (!user->IsThirdClassChangedOrMore()) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_THIRD_CLASS_TRANSFER);
        return;
    }
    if (user->Level() <= 75) {
        user->SendSystemMessage(SystemMessageId::SKILL_ENCHANT_REQUIRES_LEVEL_76_OR_HIGHER);
        return;
    }

    const std::uint64_t packed = static_cast<std::uint64_t>(skillNameId);
    const int skillLevel = static_cast<std::uint16_t>(packed >> 16);
    const std::int64_t requiredSp = GetRequiredSP(info);
    const double probability = GetEnchantProb(info);
    char buffer[20400]{};
    int bufferLength = 0;
    int itemCount = 0;
    for (const auto& required : info->RequiredItem[static_cast<int>(m_eType)]) {
        if (g_ObjectDB.GetObjectW(required.first, false) == nullptr) {
            continue;
        }
        ++itemCount;
        const int assembled =
            ::Assemble(buffer + bufferLength, 0x4f9c - bufferLength, "dd", required.first, required.second);
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    user->GetSocket()->Send(
        "chdddQddb", 0xfe, 0x5f, static_cast<int>(m_eType), info->nSkillID, PackSkillLevel(skillLevel, info->nSkillSubLevel),
        requiredSp, static_cast<int>(probability), itemCount, bufferLength, buffer
    );
}

void SkillEnchantOperator::SendAcquirableSkillList(User* const user, const std::int64_t skillNameId) {
    // Current L2Server 0x00a721f0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const std::uint64_t packed = static_cast<std::uint64_t>(skillNameId);
    const int skillId = static_cast<int>(packed >> 32);
    const int skillLevel = static_cast<std::uint16_t>(packed >> 16);
    const int skillSubLevel = static_cast<std::uint16_t>(packed);
    const std::uint32_t packedLevel = PackSkillLevel(skillLevel, skillSubLevel);

    if (!user->HaveSkill(skillId, skillLevel, skillSubLevel)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ENCHANT_UNLEARNED_SKILL);
        user->GetSocket()->Send("chdddddb", 0xfe, 0x2a, skillId, packedLevel, 0, 0, 0, 0, 0);
        return;
    }

    CSkillEnchantDB::SkillEnchantInfoVector skills;
    int hasNextLevel = 0;
    std::int64_t selectedSkillNameId = 0;
    SkillEnchantInfo* const next = g_SkillEnchantDB.GetNextLevelEnchantInfo(skillId, skillLevel, skillSubLevel);
    SkillEnchantInfo* const current = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (next == nullptr) {
        if (current == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid enchant user[%s] skill id[%d]lev[%d]sublev[%d]",
            //     user->GetName(),
            //     skillId,
            //     skillLevel,
            //     skillSubLevel)
            // L2_LOG_ERROR(Log,
            //     L"invalid enchant user[%s] skill id[%d]lev[%d]sublev[%d]",
            //     user->GetName(),
            //     skillId,
            //     skillLevel,
            //     skillSubLevel)
            // L2_LOG_ERROR(Log,
            //     "No current or next enchant data applies to skill %d level %d sublevel %d for user '%s'. Sending an empty
            //     enchant-option list.", skillId, skillLevel, skillSubLevel, WideToUtf8(user->GetName()).c_str())
            L2_LOG_ERROR(
                Log,
                "No current or next enchant data applies to skill %d level %d sublevel %d for user '%s'; sending an empty "
                "enchant-option list.",
                skillId, skillLevel, skillSubLevel, Utf16ToUtf8(user->GetName()).c_str()
            );
        } else {
            skills.push_back(current);
            g_SkillEnchantDB.GetOtherRouteSkills(skills, skillId, current);
            if (!skills.empty()) {
                selectedSkillNameId = skillNameId;
            }
        }
    } else {
        hasNextLevel = 1;
        selectedSkillNameId = PackSkillNameId(skillId, skillLevel, next->nSkillSubLevel);
        skills.push_back(next);
        g_SkillEnchantDB.GetOtherRouteSkills(skills, skillId, current != nullptr ? current : next);
    }

    char buffer[20472]{};
    int bufferLength = 0;
    for (const SkillEnchantInfo* const info : skills) {
        bufferLength +=
            ::Assemble(buffer + bufferLength, 0x4ff4 - bufferLength, "d", PackSkillLevel(skillLevel, info->nSkillSubLevel));
    }
    user->GetSocket()->Send(
        "chdddddb", 0xfe, 0x2a, skillId, packedLevel, hasNextLevel, 0, static_cast<int>(skills.size()), bufferLength, buffer
    );
    if (selectedSkillNameId > 0) {
        SendAcquirableSkillDetail(user, selectedSkillNameId);
    }
}

void SkillEnchantNormal::SendAcquiredSkillList(User* const user) {
    // Current L2Server 0x00a73324.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CSkillEnchantDB::AcquiredSkillMap acquiredSkills;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        acquiredSkills = user->m_AcquiredSkills;
    }

    CSkillEnchantDB::SkillKeyVector enchantableSkills;
    g_SkillEnchantDB.GetEnchantableSkills(acquiredSkills, enchantableSkills);
    if (enchantableSkills.empty()) {
        user->GetSocket()->Send("chdd", 0xfe, 0x29, static_cast<int>(m_eType), 0);
        return;
    }

    char buffer[20472];
    int bufferLength = 0;
    for (const CSkillKey& skill : enchantableSkills) {
        bufferLength += ::Assemble(
            buffer + bufferLength, 0x4ff4 - bufferLength, "dd", skill.m_nSkillId,
            PackSkillLevel(skill.m_nLevel, skill.m_nSubLevel)
        );
    }
    user->GetSocket()->Send(
        "chddb", 0xfe, 0x29, static_cast<int>(m_eType), static_cast<int>(enchantableSkills.size()), bufferLength, buffer
    );
}

void SkillEnchantSafe::SendAcquiredSkillList(User* const user) {
    // Current L2Server 0x00a725c4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CSkillEnchantDB::AcquiredSkillMap acquiredSkills;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        acquiredSkills = user->m_AcquiredSkills;
    }

    CSkillEnchantDB::SkillKeyVector enchantableSkills;
    g_SkillEnchantDB.GetEnchantableSkills(acquiredSkills, enchantableSkills);
    if (enchantableSkills.empty()) {
        user->GetSocket()->Send("chdd", 0xfe, 0x29, static_cast<int>(m_eType), 0);
        return;
    }

    char buffer[20472];
    int bufferLength = 0;
    for (const CSkillKey& skill : enchantableSkills) {
        bufferLength += ::Assemble(
            buffer + bufferLength, 0x4ff4 - bufferLength, "dd", skill.m_nSkillId,
            PackSkillLevel(skill.m_nLevel, skill.m_nSubLevel)
        );
    }
    user->GetSocket()->Send(
        "chddb", 0xfe, 0x29, static_cast<int>(m_eType), static_cast<int>(enchantableSkills.size()), bufferLength, buffer
    );
}

void SkillEnchantRouteChange::SendAcquiredSkillList(User* const user) {
    // Current L2Server 0x00a728a4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CSkillEnchantDB::AcquiredSkillMap acquiredSkills;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        acquiredSkills = user->m_AcquiredSkills;
    }

    CSkillEnchantDB::SkillKeyVector enchantableSkills;
    g_SkillEnchantDB.GetEnchantableSkills(acquiredSkills, enchantableSkills);
    if (enchantableSkills.empty()) {
        user->GetSocket()->Send("chdd", 0xfe, 0x29, static_cast<int>(m_eType), 0);
        return;
    }

    char buffer[20472];
    int bufferLength = 0;
    for (const CSkillKey& skill : enchantableSkills) {
        bufferLength += ::Assemble(
            buffer + bufferLength, 0x4ff4 - bufferLength, "dd", skill.m_nSkillId,
            PackSkillLevel(skill.m_nLevel, skill.m_nSubLevel)
        );
    }
    user->GetSocket()->Send(
        "chddb", 0xfe, 0x29, static_cast<int>(m_eType), static_cast<int>(enchantableSkills.size()), bufferLength, buffer
    );
}

void SkillEnchantUntrain::SendAcquiredSkillList(User*) {
    // Current L2Server 0x00a6e014 is an intentional no-op.
    L2_GUARD_FUNCTION();
}

bool SkillEnchantNormal::Operate(User* const user, const int skillId, const int skillLevelAndSubLevel) {
    // Current L2Server 0x00a731e8.
    L2_GUARD_FUNCTION();
    CAtomicSkillEnchantNormal* const job = new CAtomicSkillEnchantNormal(
        static_cast<int>(user->Id()), skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16
    );
    return job != nullptr &&
        DoOperation(user, skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16, job);
}

bool SkillEnchantSafe::Operate(User* const user, const int skillId, const int skillLevelAndSubLevel) {
    // Current L2Server 0x00a73604.
    L2_GUARD_FUNCTION();
    CAtomicSkillEnchantSafe* const job = new CAtomicSkillEnchantSafe(
        static_cast<int>(user->Id()), skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16
    );
    return job != nullptr &&
        DoOperation(user, skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16, job);
}

bool SkillEnchantRouteChange::Operate(User* const user, const int skillId, const int skillLevelAndSubLevel) {
    // Current L2Server 0x00a73740.
    L2_GUARD_FUNCTION();
    CAtomicSkillEnchantRouteChange* const job = new CAtomicSkillEnchantRouteChange(
        static_cast<int>(user->Id()), skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16
    );
    return job != nullptr &&
        DoOperation(user, skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16, job);
}

bool SkillEnchantForce::Operate(User* const user, const int skillId, const int skillLevelAndSubLevel) {
    // Current L2Server 0x00a7387c.
    L2_GUARD_FUNCTION();
    AtomicSkillEnchantForce* const job = new AtomicSkillEnchantForce(
        static_cast<int>(user->Id()), skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16
    );
    return job != nullptr &&
        DoOperation(user, skillId, static_cast<std::uint16_t>(skillLevelAndSubLevel), skillLevelAndSubLevel >> 16, job);
}

bool SkillEnchantUntrain::Operate(User*, int, int) {
    // Current L2Server 0x00a6df14.
    L2_GUARD_FUNCTION();
    return false;
}

bool SkillEnchantUntrain::OperateAtomicJob(User*, int, int, int) {
    // Current L2Server 0x00a6df94.
    L2_GUARD_FUNCTION();
    return false;
}

bool SkillEnchantNormal::OperateAtomicJob(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00a6fff8.
    L2_GUARD_FUNCTION();
    SkillEnchantInfo* const info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        return false;
    }

    const std::int64_t requiredSp = GetRequiredSP(info);
    const std::int64_t requiredAdena = GetRequiredAdena(info);
    bool succeeded;
    int selectedSubLevel;
    const bool continueEnchant = [&] {
        L2_GUARD(user->m_AcquiredSkillsSect);
        user->SPInc(-requiredSp);
        succeeded = IsEnchantSuccess(info);
        selectedSubLevel = skillSubLevel;
        if (succeeded) {
            if (user->AcquireSkill(skillId, skillLevel, skillSubLevel, false, true)) {
                user->SendAcquiredSkillToDB(skillId, skillLevel, skillSubLevel, false);
            }
        } else {
            selectedSubLevel = CalcSubLevelDown(skillSubLevel - 1);
            const CSkillInfo* downgrade = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, selectedSubLevel), nullptr);
            if (downgrade == nullptr) {
                selectedSubLevel = 0;
                downgrade = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, 0), nullptr);
                if (downgrade == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"skill acquire fail id[%d] lev [%d] sub_lev[%d] [%s][%d]",
                    //     skillId,
                    //     skillLevel,
                    //     selectedSubLevel,
                    //     L"SkillEnchant.cpp",
                    //     0xaf2)
                    // L2_LOG_ERROR(Log,
                    //     L"skill acquire fail id[%d] lev [%d] sub_lev[%d] [%s][%d]",
                    //     skillId,
                    //     skillLevel,
                    //     selectedSubLevel,
                    //     L"SkillEnchant.cpp",
                    //     0xaf2)
                    L2_LOG_ERROR(
                        Log,
                        "Failed enchantment for skill %d level %d: neither the downgrade nor base skill definition is "
                        "available; processing stops after applying the SP cost.",
                        skillId, skillLevel
                    );
                    return false;
                }
            }

            if (user->AcquireSkill(skillId, skillLevel, selectedSubLevel, true, true)) {
                user->SendAcquiredSkillToDB(skillId, skillLevel, selectedSubLevel, false);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"skill acquire fail id[%d] lev [%d] sub_lev[%d] [%s][%d]",
                //     skillId,
                //     skillLevel,
                //     selectedSubLevel,
                //     L"SkillEnchant.cpp",
                //     selectedSubLevel == 0 ? 0xaed : 0xadd)
                // L2_LOG_ERROR(Log,
                //     L"skill acquire fail id[%d] lev [%d] sub_lev[%d] [%s][%d]",
                //     skillId,
                //     skillLevel,
                //     selectedSubLevel,
                //     L"SkillEnchant.cpp",
                //     selectedSubLevel == 0 ? 0xaed : 0xadd)
                L2_LOG_ERROR(
                    Log,
                    "Cannot apply the downgrade to skill %d level %d sublevel %d after failed enchantment; this skill "
                    "change is not sent to CacheD and failure processing continues.",
                    skillId, skillLevel, selectedSubLevel
                );
            }
        }
        return true;
    }();
    if (!continueEnchant) {
        return false;
    }
    if (succeeded) {
        user->ValidateChanged(VB_MAX);
        user->m_nParamChanged |= PCB_USER;
        user->GetSocket()->Send(
            "chccdd", 0x62, SystemMessageId::SKILL_ENCHANT_SUCCEEDED, 1, 4, skillId,
            PackSkillLevel(skillLevel, skillSubLevel)
        );

        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1af, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel - 1), PackSkillLevel(skillLevel, skillSubLevel),
            requiredSp, user->m_data->m_nSP, requiredAdena, user->m_inv.GetAdenaAmount(), skillId
        );
    } else {
        user->ValidateChanged(VB_MAX);
        user->m_nParamChanged |= PCB_USER;
        user->GetSocket()->Send(
            "chccdd", 0x62, SystemMessageId::SKILL_ENCHANT_FAILED_RESET_TO_BASE_SKILL, 1, 4, skillId,
            PackSkillLevel(skillLevel, skillSubLevel)
        );

        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1b0, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel), PackSkillLevel(skillLevel, selectedSubLevel),
            requiredSp, user->m_data->m_nSP, requiredAdena, user->m_inv.GetAdenaAmount(), skillId
        );
    }

    user->GetSocket()->Send("chd", 0xfe, 0xa8, succeeded ? 1 : 0);
    SendAcquirableSkillList(user, PackSkillNameId(skillId, skillLevel, selectedSubLevel));
    user->m_ShortCut.SendShortCutInfo();
    return true;
}

bool SkillEnchantSafe::OperateAtomicJob(User* const user, const int skillId, const int skillLevel, const int skillSubLevel) {
    // Current L2Server 0x00a7099c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    SkillEnchantInfo* const info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        return false;
    }

    const bool succeeded = IsEnchantSuccess(info);
    const std::int64_t requiredSp = GetRequiredSP(info);
    const std::int64_t requiredAdena = GetRequiredAdena(info);
    std::int64_t selectedSkillNameId;
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        user->SPInc(-requiredSp);
        if (user->m_data->m_nSP < 0) {
            user->m_data->m_nSP = 0;
        }

        selectedSkillNameId = 0;
        if (succeeded) {
            if (user->AcquireSkill(info->nSkillID, skillLevel, skillSubLevel, false, true)) {
                user->SendAcquiredSkillToDB(info->nSkillID, skillLevel, skillSubLevel, false);
            }
        }
    }
    if (succeeded) {
        user->ValidateChanged(VB_MAX);
        user->m_nParamChanged |= PCB_USER;
        user->GetSocket()->Send(
            "chccdd", 0x62, SystemMessageId::SKILL_ENCHANT_SUCCEEDED, 1, 4, skillId,
            PackSkillLevel(skillLevel, skillSubLevel)
        );
        selectedSkillNameId = PackSkillNameId(skillId, skillLevel, skillSubLevel);

        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1b3, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel),
            PackSkillLevel(skillLevel, info->nNeedSkillSubLevel), requiredSp, user->m_data->m_nSP, requiredAdena,
            user->m_inv.GetAdenaAmount(), skillId
        );
    } else {
        user->GetSocket()->Send(
            "chccdd", 0x62,
            info->nEnchantId < 2 ? SystemMessageId::SKILL_ENCHANT_FAILED_RESET_TO_BASE_SKILL
                                 : SystemMessageId::SKILL_ENCHANT_FAILED_LEVEL_PRESERVED,
            1, 4, skillId, PackSkillLevel(skillLevel, skillSubLevel)
        );
        user->SendSkillList(nullptr, true, 0);
        selectedSkillNameId = PackSkillNameId(skillId, skillLevel, info->nNeedSkillSubLevel);

        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1b4, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel), PackSkillLevel(skillLevel, skillSubLevel),
            requiredSp, user->m_data->m_nSP, requiredAdena, user->m_inv.GetAdenaAmount(), skillId
        );
    }

    user->GetSocket()->Send("chd", 0xfe, 0xa8, succeeded ? 1 : 0);
    SendAcquirableSkillList(user, selectedSkillNameId);
    if (succeeded) {
        user->m_ShortCut.SendShortCutInfo();
    }
    return true;
}

bool SkillEnchantRouteChange::OperateAtomicJob(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00a70dfc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    SkillEnchantInfo* const info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        return false;
    }

    const std::int64_t requiredSp = GetRequiredSP(info);
    const std::int64_t requiredAdena = GetRequiredAdena(info);
    const int oldSubLevel = user->GetAcquiredSkillSubLevel(skillId);
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        user->SPInc(-requiredSp);
        if (user->m_data->m_nSP < 0) {
            user->m_data->m_nSP = 0;
        }
        if (user->AcquireSkill(info->nSkillID, skillLevel, skillSubLevel, true, true)) {
            user->SendAcquiredSkillToDB(info->nSkillID, skillLevel, skillSubLevel, true);
        }
        user->ValidateChanged(VB_MAX);
    }
    user->m_nParamChanged |= PCB_USER;
    user->GetSocket()->Send(
        "chccdd", 0x62, SystemMessageId::SKILL_ENCHANT_ROUTE_CHANGED_LEVEL_PRESERVED, 2, 4, skillId,
        PackSkillLevel(skillLevel, skillSubLevel)
    );
    user->GetSocket()->Send("chd", 0xfe, 0xa8, 1);
    SendAcquirableSkillList(user, PackSkillNameId(skillId, skillLevel, skillSubLevel));
    user->m_ShortCut.SendShortCutInfo();

    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1b2, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel), PackSkillLevel(skillLevel, oldSubLevel), requiredSp,
        user->m_data->m_nSP, requiredAdena, user->m_inv.GetAdenaAmount(), skillId
    );
    return true;
}

bool SkillEnchantForce::OperateAtomicJob(
    User* const user, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server 0x00a71118.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    SkillEnchantInfo* const info = g_SkillEnchantDB.GetSkillEnchantInfo(skillId, skillLevel, skillSubLevel);
    if (info == nullptr) {
        return false;
    }

    const std::int64_t requiredSp = GetRequiredSP(info);
    const std::int64_t requiredAdena = GetRequiredAdena(info);
    {
        L2_GUARD(user->m_AcquiredSkillsSect);
        user->SPInc(-requiredSp);
        if (user->m_data->m_nSP < 0) {
            user->m_data->m_nSP = 0;
        }
        if (user->AcquireSkill(info->nSkillID, skillLevel, skillSubLevel, true, true)) {
            user->SendAcquiredSkillToDB(info->nSkillID, skillLevel, skillSubLevel, true);
        }
        user->ValidateChanged(VB_MAX);
    }
    user->ValidateChanged(VB_MAX);
    user->m_nParamChanged |= PCB_USER;
    user->GetSocket()->Send(
        "chccdd", 0x62, SystemMessageId::SKILL_ENCHANT_SUCCEEDED, 1, 4, skillId, PackSkillLevel(skillLevel, skillSubLevel)
    );

    CLogFormatter formatter;
    formatter.WriteLog2(
        0x1b3, user, nullptr, PackSkillLevel(skillLevel, skillSubLevel),
        PackSkillLevel(skillLevel, info->nNeedSkillSubLevel), requiredSp, user->m_data->m_nSP, requiredAdena,
        user->m_inv.GetAdenaAmount(), info->nSkillID
    );
    user->GetSocket()->Send("chd", 0xfe, 0xa8, 1);
    SendAcquirableSkillList(user, PackSkillNameId(skillId, skillLevel, skillSubLevel));
    user->m_ShortCut.SendShortCutInfo();
    return true;
}

CSkillEnchantDB g_SkillEnchantDB;

SkillEnchantInfo::SkillEnchantInfo() :
    nSkillID(0),
    nMinSkillLevel(0),
    nMaxSkillLevel(0),
    nSkillSubLevel(0),
    nNeedSkillSubLevel(0),
    nRouteId(0),
    nEnchantId(0),
    nSp{},
    dProb{},
    RequiredItem{} {}

CSkillEnchantDB::CSkillEnchantDB() = default;

CSkillEnchantDB::~CSkillEnchantDB() = default;

void CSkillEnchantDB::Add(SkillEnchantInfo* info) {
    L2_GUARD_FUNCTION();

    if (info == nullptr) {
        // Log.Add(LOG_ERROR, L"SkillEnchantInfo is null!!!!!!")
        // L2_LOG_ERROR(Log, L"SkillEnchantInfo is null!!!!!!")
        L2_LOG_ERROR(Log, "Cannot register skill-enchant data: the record is null.");
        return;
    }
    if (info->nSkillSubLevel < 1000) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] enchant sublevel has problem[id:%d][route:%d][enchant:%d][sublevel:%d]",
        //     L"SkillEnchant.cpp",
        //     0x53,
        //     info->nSkillID,
        //     info->nRouteId,
        //     info->nEnchantId,
        //     info->nSkillSubLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] enchant sublevel has problem[id:%d][route:%d][enchant:%d][sublevel:%d]",
        //     L"SkillEnchant.cpp",
        //     0x53,
        //     info->nSkillID,
        //     info->nRouteId,
        //     info->nEnchantId,
        //     info->nSkillSubLevel)
        L2_LOG_ERROR(
            Log,
            "skillenchantdata.txt: skill %d route %d enchant step %d has sublevel %d below the minimum 1000; the record is "
            "not registered.",
            info->nSkillID, info->nRouteId, info->nEnchantId, info->nSkillSubLevel
        );
        return;
    }

    const EnchantKey key(info->nSkillID, info->nRouteId, info->nEnchantId);
    const auto [position, inserted] = m_SkillEnchantMap.emplace(key, info);
    static_cast<void>(position);
    if (!inserted) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated skill enchant info[id:%d][route:%d][enchant:%d]",
        //     L"SkillEnchant.cpp",
        //     0x5e,
        //     info->nSkillID,
        //     info->nRouteId,
        //     info->nEnchantId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] duplicated skill enchant info[id:%d][route:%d][enchant:%d]",
        //     L"SkillEnchant.cpp",
        //     0x5e,
        //     info->nSkillID,
        //     info->nRouteId,
        //     info->nEnchantId)
        L2_LOG_ERROR(
            Log,
            "skillenchantdata.txt: duplicate skill %d route %d enchant step %d; the existing enchant record is retained.",
            info->nSkillID, info->nRouteId, info->nEnchantId
        );
    }
}

void CSkillEnchantDB::SetSkillNeedLevel() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_SkillEnchantMap) {
        SkillEnchantInfo* const info = entry.second;
        if (info->nEnchantId == 1) {
            info->nNeedSkillSubLevel = 0;
            continue;
        }

        const EnchantKey previousKey(info->nSkillID, info->nRouteId, info->nEnchantId - 1);
        const auto previous = m_SkillEnchantMap.find(previousKey);
        if (previous != m_SkillEnchantMap.end()) {
            info->nNeedSkillSubLevel = previous->second->nSkillSubLevel;
        }
        if (info->nNeedSkillSubLevel == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] failed SetSkillNeedLevel for Enchant Skill[ID(%d) subLevel(%d)]",
            //     L"SkillEnchant.cpp",
            //     0xb7,
            //     info->nSkillID,
            //     0)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] failed SetSkillNeedLevel for Enchant Skill[ID(%d) subLevel(%d)]",
            //     L"SkillEnchant.cpp",
            //     0xb7,
            //     info->nSkillID,
            //     0)
            L2_LOG_ERROR(
                Log,
                "skillenchantdata.txt: cannot set a nonzero prerequisite sublevel for skill %d route %d enchant step %d "
                "sublevel %d; the prerequisite remains 0.",
                info->nSkillID, info->nRouteId, info->nEnchantId, info->nSkillSubLevel
            );
        }
    }
}

void CSkillEnchantDB::MakeMap() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_SkillEnchantMap) {
        SkillEnchantInfo* const info = entry.second;

        const SkillKey enchantSkillKey(info->nSkillID, info->nSkillSubLevel);
        const auto [position, inserted] = m_mapSkillID2EnchantSkill.emplace(enchantSkillKey, info);
        static_cast<void>(position);
        if (!inserted) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] already exists for Enchant Skill[ID(%d) SkillSubLevel(%d)]",
            //     L"SkillEnchant.cpp",
            //     0xed,
            //     info->nSkillID,
            //     info->nSkillSubLevel)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] already exists for Enchant Skill[ID(%d) SkillSubLevel(%d)]",
            //     L"SkillEnchant.cpp",
            //     0xed,
            //     info->nSkillID,
            //     info->nSkillSubLevel)
            L2_LOG_ERROR(
                Log,
                "skillenchantdata.txt: duplicate lookup entry for skill %d sublevel %d; the existing entry is retained and "
                "other enchant maps are still updated.",
                info->nSkillID, info->nSkillSubLevel
            );
        }

        m_mmapSkillID2OtherEnchantSkill.emplace(SkillKey(info->nSkillID, info->nEnchantId), info);

        const int normalMaxLevel = g_SkillDB.GetNormalSkillMaxLevel(info->nSkillID);
        for (int level = info->nMinSkillLevel; level <= normalMaxLevel; ++level) {
            if (g_SkillDB.GetSkillInfo(CSkillKey(info->nSkillID, level, info->nSkillSubLevel), nullptr) == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"skill id[%d] lev[%d], don't have sublev[%d]",
                //     info->nSkillID,
                //     level,
                //     info->nSkillSubLevel)
                // L2_LOG_ERROR(Log,
                //     L"skill id[%d] lev[%d], don't have sublev[%d]",
                //     info->nSkillID,
                //     level,
                //     info->nSkillSubLevel)
                L2_LOG_ERROR(
                    Log,
                    "skillenchantdata.txt: skill %d level %d sublevel %d has no available definition; this enchant record's "
                    "maximum level is capped at the preceding level.",
                    info->nSkillID, level, info->nSkillSubLevel
                );
                info->nMaxSkillLevel = level - 1;
                break;
            }
            if (level == normalMaxLevel) {
                info->nMaxSkillLevel = level;
            }
        }

        m_mmapNeedSkill2EnchantInfo.emplace(SkillKey(info->nSkillID, info->nNeedSkillSubLevel), info);
    }
}

int CSkillEnchantDB::GetRewardSP(int, int, int) {
    // Current L2Server 0x00a6d64c is an intentional no-op.
    L2_GUARD_FUNCTION();
    return 0;
}

int CSkillEnchantDB::GetRewardItemCount(int, int, int) {
    // Current L2Server 0x00a6d6cc is an intentional no-op.
    L2_GUARD_FUNCTION();
    return 0;
}

SkillEnchantInfo* CSkillEnchantDB::GetSkillEnchantInfo(int skillId, int normalLevel, int subLevel) {
    // Current L2Server 0x00a6f788.
    L2_GUARD_FUNCTION();
    const auto found = m_mapSkillID2EnchantSkill.find(SkillKey(skillId, subLevel));
    if (found == m_mapSkillID2EnchantSkill.end()) {
        return nullptr;
    }
    SkillEnchantInfo* const info = found->second;
    return info->nMinSkillLevel <= normalLevel && normalLevel <= info->nMaxSkillLevel ? info : nullptr;
}

SkillEnchantInfo* CSkillEnchantDB::GetNextLevelEnchantInfo(int skillId, int normalLevel, int enchantId) {
    // Current L2Server 0x00a6f85c.
    L2_GUARD_FUNCTION();
    const auto found = m_mmapNeedSkill2EnchantInfo.find(SkillKey(skillId, enchantId));
    if (found == m_mmapNeedSkill2EnchantInfo.end()) {
        return nullptr;
    }
    SkillEnchantInfo* const info = found->second;
    return info->nMinSkillLevel <= normalLevel && normalLevel <= info->nMaxSkillLevel ? info : nullptr;
}

bool CSkillEnchantDB::IsEnchantableSkill(int skillId, int normalLevel, int subLevel) {
    // Current L2Server 0x00a715b4.
    L2_GUARD_FUNCTION();
    return GetNextLevelEnchantInfo(skillId, normalLevel, subLevel) != nullptr ||
        GetSkillEnchantInfo(skillId, normalLevel, subLevel) != nullptr;
}

void CSkillEnchantDB::GetEnchantableSkills(const AcquiredSkillMap& acquiredSkills, SkillKeyVector& result) {
    // Current L2Server 0x00a71dcc.
    L2_GUARD_FUNCTION();
    result.clear();
    for (const auto& acquired : acquiredSkills) {
        const SkillKey key(acquired.first, acquired.second.m_nSubLevel);
        if (m_mmapNeedSkill2EnchantInfo.find(key) == m_mmapNeedSkill2EnchantInfo.end()) {
            continue;
        }
        const CSkillKey enchantable(acquired.first, acquired.second.m_nLevel, acquired.second.m_nSubLevel);
        result.push_back(enchantable);
    }
}

void CSkillEnchantDB::GetOtherRouteSkills(
    SkillEnchantInfoVector& result, const int skillId, SkillEnchantInfo* const current
) {
    // Current L2Server 0x00a720ac.
    L2_GUARD_FUNCTION();
    if (current == nullptr) {
        return;
    }
    const auto range = m_mmapSkillID2OtherEnchantSkill.equal_range(SkillKey(skillId, current->nEnchantId));
    for (auto position = range.first; position != range.second; ++position) {
        SkillEnchantInfo* const candidate = position->second;
        if (candidate->nEnchantId == current->nEnchantId && candidate->nRouteId != current->nRouteId) {
            result.push_back(candidate);
        }
    }
}

void CSkillEnchantDB::PostProcess() {
    L2_GUARD_FUNCTION();
    SetSkillNeedLevel();
    MakeMap();
}

void CSkillEnchantDB::Load() {
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\skillenchantdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/skillenchantdata.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"SkillEnchant.cpp",
        //     0x28,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"SkillEnchant.cpp",
        //     0x28,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read skillenchantdata.txt; loading stops and existing enchant data is retained.");
        return;
    }

    CParserForSkillEnchant parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForSkillEnchant::create failed",
        //     L"SkillEnchant.cpp",
        //     0x31)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForSkillEnchant::create failed",
        //     L"SkillEnchant.cpp",
        //     0x31)
        L2_LOG_ERROR(Log, "Cannot initialize the skillenchantdata.txt parser; existing enchant data is retained.");
        return;
    }

    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() == 0) {
        PostProcess();
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Parsing %s file failed after line[%d]",
    //     L"SkillEnchant.cpp",
    //     0x3d,
    //     kFileName,
    //     parser.m_lexer.line)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Parsing %s file failed after line[%d]",
    //     L"SkillEnchant.cpp",
    //     0x3d,
    //     kFileName,
    //     parser.m_lexer.line)
    L2_LOG_ERROR(
        Log,
        "Parsing skillenchantdata.txt stopped at line %lld; records already read are retained and derived enchant maps are "
        "not rebuilt.",
        static_cast<long long>(parser.m_lexer.line) + 1
    );
}

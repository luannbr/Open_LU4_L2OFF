#include "Skill.h"
#include "ParserSkill.h"
#include "ParserSkillSynergy.h"
#include "Protocol/src/SystemMessageIds.h"

#include "Creature.h"
#include "CursedWeapon.h"
#include "Door.h"
#include "Duel.h"
#include "Item.h"
#include "MiniGameAgit.h"
#include "NewAction.h"
#include "ObjectDB.h"
#include "Pet.h"
#include "SerializedMessage.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "SkillFxFunc.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

// Current L2Server .data 0x015e4170.
constexpr double kMpConsumeBonus[20] = {
    1.0,
    1.5,
    1.7,
    2.5,
    4.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
    6.0,
};

std::u16string TargetSkillName(const wchar_t* value) {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        std::u16string result;
        while (*value != L'\0') {
            result.push_back(static_cast<char16_t>(*value++));
        }
        return result;
    }
    return ::WideToUtf16(std::wstring_view(value));
}

void SkillEffectsOnPostLoad(CSkillInfo::SkillEffects& effects) {
    // Current L2Server 0x00a64b3c assumes every published
    // pointer is non-null and calls CSkillEffect::OnPostLoad (+0x90).
    for (CSkillEffect* const effect : effects) {
        effect->OnPostLoad();
    }
}

void SendSkillReactionMessage(User& recipient, CObject& subject, int messageId, const CSkillInfo& skillInfo) {
    const int encodedLevel = skillInfo.m_nSubLevel << 16 | static_cast<std::uint16_t>(skillInfo.m_nLevel);
    if (subject.IsPC()) {
        User* const subjectUser = subject.CastUser();
        recipient.GetSocket()->Send(
            "chccScdd", 0x62, messageId, 2, 12, subjectUser->GetName2(&recipient), 4, skillInfo.m_nSkillId, encodedLevel
        );
        return;
    }

    recipient.GetSocket()->Send(
        "chccdcdd", 0x62, messageId, 2, 2, subject.CastNPC()->GetNPCId(), 4, skillInfo.m_nSkillId, encodedLevel
    );
}

void SetAsCombatMode(CCreature* creature, CObject* target) {
    // Current L2Server 0x00a64090.
    L2_GUARD_FUNCTION();
    const bool creatureWasNotInCombat = !creature->m_data->m_bIsInCombatMode;
    if (creatureWasNotInCombat) {
        creature->SetInCombatMode(true);
    }
    CIOObject::SetTimeStamp(&creature->m_nCombatModeTimeStamp);
    creature->CheckSummonBattleTimeStamp();
    if (creatureWasNotInCombat) {
        creature->BroadcastCombatModeStart();
    }

    if (target == nullptr || !target->IsCreature()) {
        return;
    }
    CCreature* const targetCreature = target->CastCreature();
    const bool targetWasNotInCombat = !targetCreature->m_data->m_bIsInCombatMode;
    if (targetWasNotInCombat) {
        targetCreature->SetInCombatMode(true);
    }
    CIOObject::SetTimeStamp(&targetCreature->m_nCombatModeTimeStamp);
    targetCreature->CheckSummonBattleTimeStamp();
    if (targetWasNotInCombat || creatureWasNotInCombat) {
        targetCreature->BroadcastCombatModeStart();
    }
}

} // namespace

bool CSkillInfo::IsActiveSkill() const {
    switch (m_OperateType) {
        case SKILL_INSTANT:
        case SKILL_DURATION:
        case SKILL_DURATION_LEVEL_UP:
        case SKILL_SELFDURATION:
        case SKILL_TOGGLE:
        case SKILL_CHANNELLING_INSTANCE:
        case SKILL_CHANNELLING_DURATION:
        case SKILL_CHANNELLING_DURATION_LEVEL_UP:
        case SKILL_CHARGE_INSTANT:
        case SKILL_CHARGE_DURATION:
        case SKILL_AURA:
        case SKILL_AURA_ACTIVITY:
        case SKILL_GROUP_TOGGLE:
        case SKILL_BACK_STEP_INSTANT:
        case SKILL_BACK_STEP_DURATION:
        case SKILL_AURA_ACTIVITY2:
        case SKILL_PARTY_INSTANT:
        case SKILL_DUAL_PUMP1:
        case SKILL_BLINK_INSTANT:
        case SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK:
            return true;
        default:
            return false;
    }
}

void CSkillInfo::SendRegenPacket(User* user, int remainingTime) const {
    L2_GUARD_FUNCTION();

    for (CSkillEffect* const effect : m_TickEffects) {
        CSkillTickEffect* const tickEffect = effect->CastTickEffect();
        if (tickEffect == nullptr) {
            continue;
        }

        std::array<char, 0x2000> packet{};
        const int assembled = ::Assemble(
            packet.data(), static_cast<int>(packet.size()), "dddf", tickEffect->GetTickEffectType(), remainingTime,
            tickEffect->nUnitTick, tickEffect->Amount()
        );
        const int packetSize = assembled > 0 ? assembled : 0;
        user->GetSocket()->Send("chb", 0xfe, 1, packetSize, packet.data());
    }
}

CSkillDB::CSkillDB() :
    m_bNameLookupMiss(false),
    m_bKeyLookupMiss(false),
    m_pReserved68(nullptr),
    m_pReserved70(nullptr),
    m_pReserved78(nullptr),
    m_bOnlyPrecompiledHeader(false),
    m_pPreviousSkillInfo(nullptr) {}

CSkillDB::~CSkillDB() = default;

int CSkillDB::GetNormalSkillMaxLevel(int skillId) {
    L2_GUARD_FUNCTION();
    const auto found = m_NormalSkillMaxLevelMap.find(skillId);
    return found == m_NormalSkillMaxLevelMap.end() ? 0 : found->second;
}

int CSkillDB::GetSkillIdLevelByName(const wchar_t* name, int* skillId, int* level, int* subLevel) {
    L2_GUARD_FUNCTION();
    if (!name) {
        return 0;
    }

    const std::u16string targetName = TargetSkillName(name);
    const auto found = m_SkillNameMap.find(targetName.c_str());
    if (found == m_SkillNameMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unregistered skill by name (%s)",
        //     L"Skill.cpp",
        //     0xb81,
        //     name)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unregistered skill by name (%s)",
        //     L"Skill.cpp",
        //     0xb81,
        //     name)
        L2_LOG_ERROR(
            Log, "No skill is registered for lookup name '%s'; output IDs and levels are left unchanged.",
            Utf16ToUtf8(targetName).c_str()
        );
        if (!m_bNameLookupMiss) {
            m_bNameLookupMiss = true;
        }
        return 0;
    }

    const CSkillInfo* const skillInfo = found->second;
    *skillId = skillInfo->m_nSkillId;
    *level = skillInfo->m_nLevel;
    *subLevel = skillInfo->m_nSubLevel;
    return 1;
}

CSkillKey CSkillDB::GetSkillKeyByName(wchar_t* name) {
    L2_GUARD_FUNCTION();
    int skillId = 0;
    int level = 0;
    int subLevel = 0;
    if (!GetSkillIdLevelByName(name, &skillId, &level, &subLevel)) {
        return CSkillKey();
    }
    return CSkillKey(skillId, level, subLevel);
}

void CSkillDB::Add(CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    const CSkillKey skillKey(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel);
    if (m_SkillMap.find(skillKey) != m_SkillMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated skill (%s)",
        //     L"Skill.cpp",
        //     0xc0d,
        //     skillInfo->m_pszName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated skill (%s)",
        //     L"Skill.cpp",
        //     0xc0d,
        //     skillInfo->m_pszName)
        L2_LOG_ERROR(
            Log, "Duplicate skill ID %d level %u sublevel %u for '%s'; the existing skill is retained.", skillKey.m_nSkillId,
            static_cast<unsigned>(skillKey.m_nLevel), static_cast<unsigned>(skillKey.m_nSubLevel),
            skillInfo->m_pszName == nullptr ? "<null>"
                                            : Utf16ToUtf8(target_utf16::FromHostCodeUnits(skillInfo->m_pszName)).c_str()
        );
        return;
    }

    const std::u16string targetName = TargetSkillName(skillInfo->m_pszName);
    if (m_SkillNameMap.find(targetName.c_str()) != m_SkillNameMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated skill (%s)",
        //     L"Skill.cpp",
        //     0xc15,
        //     skillInfo->m_pszName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated skill (%s)",
        //     L"Skill.cpp",
        //     0xc15,
        //     skillInfo->m_pszName)
        L2_LOG_ERROR(
            Log, "Duplicate skill lookup name '%s' for ID %d level %u sublevel %u; the new definition is not registered.",
            Utf16ToUtf8(targetName).c_str(), skillKey.m_nSkillId, static_cast<unsigned>(skillKey.m_nLevel),
            static_cast<unsigned>(skillKey.m_nSubLevel)
        );
        return;
    }

    m_SkillMap[skillKey] = skillInfo;
    m_SkillNameMap[targetName] = skillInfo;

    if (skillInfo->m_nLevel <= 0) {
        return;
    }
    const auto maxLevel = m_NormalSkillMaxLevelMap.find(skillInfo->m_nSkillId);
    if (maxLevel == m_NormalSkillMaxLevelMap.end()) {
        m_NormalSkillMaxLevelMap.emplace(skillInfo->m_nSkillId, skillInfo->m_nLevel);
    } else if (maxLevel->second < skillInfo->m_nLevel) {
        maxLevel->second = skillInfo->m_nLevel;
    }
}

void CSkillDB::PostLoad() {
    // Current L2Server 0x00a6b668.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"CSkillDB::PostLoad()")
    // Noise: The startup loader already announces skill post-load initialization immediately before this call.
    // L2_LOG_INFO(Log, L"CSkillDB::PostLoad()")

    for (const auto& entry : m_SkillMap) {
        entry.second->PostLoad();
    }

    g_SkillInfoTempDataDB.clear();
    g_SkillInfoVerifyDataDB.clear();
}

const CSkillInfo* CSkillDB::GetSkillInfo(CSkillKey skillKey, CCreature* creature) {
    L2_GUARD_FUNCTION();
    const auto found = m_SkillMap.find(skillKey);
    if (found != m_SkillMap.end()) {
        return found->second;
    }

    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unregistered skill(id:%d-level:%d)",
        //     L"Skill.cpp",
        //     0xda4,
        //     skillKey.m_nSkillId,
        //     skillKey.m_nLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unregistered skill(id:%d-level:%d)",
        //     L"Skill.cpp",
        //     0xda4,
        //     skillKey.m_nSkillId,
        //     skillKey.m_nLevel)
        L2_LOG_ERROR(
            Log, "Skill ID %d level %u sublevel %u is not registered; lookup returns no definition.", skillKey.m_nSkillId,
            static_cast<unsigned>(skillKey.m_nLevel), static_cast<unsigned>(skillKey.m_nSubLevel)
        );
    } else {
        const wchar_t* ownerType = L"Unknown class";
        if (creature->IsNPC()) {
            ownerType = L"NPC";
        } else if (creature->IsPC()) {
            ownerType = L"User";
            User* const user = creature->CastUser();
            if (skillKey.m_nSkillId != 0x13d4 && skillKey.m_nSkillId != 0x38eb && skillKey.m_nSkillId != 0x5b30) {
                user->LogAbnormalAction(AUA_TRY_TO_USE_UNREGISTERED_SKILL, !user->IsBuilder(), 0, 0, 0, 0);
            }
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [%s][%s] USE Unregistered skill(id:%d-level:%d)",
        //     L"Skill.cpp",
        //     0xda0,
        //     ownerType,
        //     creature->GetName(),
        //     skillKey.m_nSkillId,
        //     skillKey.m_nLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [%s][%s] USE Unregistered skill(id:%d-level:%d)",
        //     L"Skill.cpp",
        //     0xda0,
        //     ownerType,
        //     creature->GetName(),
        //     skillKey.m_nSkillId,
        //     skillKey.m_nLevel)
        // L2_LOG_ERROR(
        //     Log, "Skill lookup failed for %s '%s': ID %d level %u sublevel %u is not registered.",
        //     WideToUtf8(ownerType).c_str(), WideToUtf8(creature->GetName()).c_str(), skillKey.m_nSkillId,
        //     static_cast<unsigned>(skillKey.m_nLevel), static_cast<unsigned>(skillKey.m_nSubLevel)
        // )
        L2_LOG_ERROR(
            Log, "Skill lookup failed for %s '%s': ID %d level %u sublevel %u is not registered.",
            WideToUtf8(ownerType).c_str(), Utf16ToUtf8(creature->GetName()).c_str(), skillKey.m_nSkillId,
            static_cast<unsigned>(skillKey.m_nLevel), static_cast<unsigned>(skillKey.m_nSubLevel)
        );
    }

    runtime::process::ReportNamedCallStack(L"SkillInfo", 2, false);
    if (!m_bKeyLookupMiss) {
        m_bKeyLookupMiss = true;
    }
    return nullptr;
}

const CSkillInfo* CSkillDB::GetSkillInfoByName(const wchar_t* name) {
    L2_GUARD_FUNCTION();
    int skillId = 0;
    int level = 0;
    int subLevel = 0;
    if (!GetSkillIdLevelByName(name, &skillId, &level, &subLevel)) {
        return nullptr;
    }
    return GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr);
}

bool CSkillDB::LoadSkillData(char16_t* data, int dataLength, bool reload) {
    L2_GUARD_FUNCTION();

    CParserForSkill parser;
    if (!parser.Create(data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForSkill::create failed",
        //     L"Skill.cpp",
        //     0xc88)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForSkill::create failed",
        //     L"Skill.cpp",
        //     0xc88)
        L2_LOG_ERROR(
            Log, "Cannot initialize the skilldata.txt parser for %s; toggle groups have been cleared and loading stops.",
            reload ? "reload" : "initial load"
        );
        return false;
    }

    parser.m_lexer.yybegin(4);
    parser.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    parser.m_bReload = reload;
    parser.m_lexer.m_bOnlyPrecompiledHeader = parser.m_bOnlyPrecompiledHeader;

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing SkillData file failed after line[%d]",
        //     L"Skill.cpp",
        //     0xc98,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing SkillData file failed after line[%d]",
        //     L"Skill.cpp",
        //     0xc98,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(
            Log, "Parsing skilldata.txt stopped at line %lld during %s; partial skill-data changes are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1, reload ? "reload" : "initial load"
        );
        return false;
    }
    return true;
}

bool CSkillDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\skilldata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/skilldata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Skill.cpp",
        //     0xc6f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Skill.cpp",
        //     0xc6f)
        L2_LOG_ERROR(Log, "Cannot read skilldata.txt; existing skill data is retained.");
        return false;
    }

    return LoadSkillData(input.GetRawString() + 1, input.GetRawStringLen() - 1, false);
}

bool CSkillDB::LoadSkillSynergyData(char16_t* data, int dataLength, bool reload) {
    L2_GUARD_FUNCTION();

    CParserForSkillSynergy parser;
    if (!parser.Create(data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForSkillSynergy::create failed",
        //     L"Skill.cpp",
        //     0xcc5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForSkillSynergy::create failed",
        //     L"Skill.cpp",
        //     0xcc5)
        L2_LOG_ERROR(Log, "Cannot initialize the skillsynergydata.txt parser; synergy rules have been cleared.");
        return false;
    }

    (void)reload;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"Skill.cpp",
        //     0xcd0,
        //     L"..\\Script\\skillsynergydata.txt",
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"Skill.cpp",
        //     0xcd0,
        //     L"..\\Script\\skillsynergydata.txt",
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Parsing skillsynergydata.txt stopped at line %lld; synergy rules already published by this load are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        return false;
    }
    return true;
}

bool CSkillDB::LoadSkillSynergy() {
    L2_GUARD_FUNCTION();

    m_mapSynergySkill.clear();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\skillsynergydata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/skillsynergydata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Skill.cpp",
        //     0xcb0,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Skill.cpp",
        //     0xcb0,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read skillsynergydata.txt; synergy rules have been cleared and loading stops.");
        return false;
    }

    return LoadSkillSynergyData(input.GetRawString() + 1, input.GetRawStringLen() - 1, false);
}

AbnormalStatusSlotType GetAbnormalStatusSlotTypeByName(char16_t* slotName) {
    L2_GUARD_FUNCTION();
    return skill_parser_detail::FindAbnormalByName(slotName, 0);
}

void GetAbnormalStatusSlotTypeByName(const std::vector<char16_t*>& words, std::vector<int>& abnormalTypes) {
    L2_GUARD_FUNCTION();

    for (char16_t* const word : words) {
        const AbnormalStatusSlotType abnormalType = skill_parser_detail::FindAbnormalByName(word, 0);
        if (abnormalType == ABNS_INVALID) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] AbnormalStatusSlotType is invalid",
            //     word)
            // L2_LOG_ERROR(Log,
            //     L"[%s] AbnormalStatusSlotType is invalid",
            //     word)
            L2_LOG_ERROR(
                Log, "Abnormal-status slot '%s' resolves to the invalid slot value -1; this entry is omitted.",
                Utf16ToUtf8(word).c_str()
            );
        } else {
            abnormalTypes.push_back(static_cast<int>(abnormalType));
        }
    }
}

std::map<CSkillKey, CSkillInfoVerifyData> g_SkillInfoVerifyDataDB;
std::map<CSkillKey, CSkillInfoTempData> g_SkillInfoTempDataDB;
std::map<int, std::vector<int>> m_mapToggleGroup;
std::map<int, TriggerSynergySkillInfo*> m_mapSynergySkill;
CSkillDB g_SkillDB;
CSkillDB g_OptionSkillDB;
std::map<std::int64_t, bool> g_ItemSkillTransfer;

FAngle::Enum ConvertSkillFxPositionTypeToAngleDir(const SkillFxPositionType positionType) {
    if (positionType != SFPT_SIDE) {
        if (positionType == SFPT_BACK) {
            return FAngle::Back;
        }
        if (positionType < SFPT_LEFT || SFPT_RIGHT < positionType) {
            return FAngle::Front;
        }
    }
    return FAngle::Side;
}

CEffectCondition::CEffectCondition() = default;

bool CEffectCondition::CheckEffectCondition(CCreature*) { return true; }

CEffectCondition::~CEffectCondition() = default;

CSkillEffect::CSkillEffect() : nUnitTick(0), eEffectType(EFT_FATAL_BLOW) {}

void CSkillEffect::CalcSuccess(CCreature*, CObject*, const CSkillInfo*, CSkillAction2*) {}

void CSkillEffect::CalcSuccess(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*) {}

bool CSkillEffect::InstantByArea(CObject*, const CSkillInfo*) { return true; }

bool CSkillEffect::InstantByArea(CWorldObject*, const CSkillInfo*) { return true; }

bool CSkillEffect::Instant(CCreature*, CObject*, const CSkillInfo*, CSkillAction2*, double) { return true; }

bool CSkillEffect::Instant(CCreature*, CWorldObject*, const CSkillInfo*, CSkillAction2*, double) { return true; }

bool CSkillEffect::Tick(CObject*, double, unsigned, const CSkillInfo*) { return true; }

bool CSkillEffect::Tick(CWorldObject*, double, unsigned, const CSkillInfo*) { return true; }

double CSkillEffect::Amount() { return 0.0; }

bool CSkillEffect::CheckPumpCondition(CCreature*) { return true; }

bool CSkillEffect::Pump(CCreature*, const CSkillInfo*, int) { return true; }

bool CSkillEffect::PumpStart(CCreature&, CCreature*, const CSkillInfo*, SkillPumpStartType) const { return true; }

bool CSkillEffect::PumpEnd(CCreature&, CCreature*, const CSkillInfo*, SkillPumpEndType) const { return true; }

bool CSkillEffect::Consume(CObject*, double) { return true; }

bool CSkillEffect::IsTickEffect() const { return false; }

bool CSkillEffect::IsConsumeEffect() const { return false; }

CSkillTickEffect* CSkillEffect::CastTickEffect() { return nullptr; }

CSkillConsumeEffect* CSkillEffect::CastConsumeEffect() { return nullptr; }

void CSkillEffect::OnPostLoad() {}

CSkillEffect::~CSkillEffect() = default;

bool CSkillEffect::IsConsumeAgathionEnergyEffect() const { return false; }

CSkillTickEffect::CSkillTickEffect(double amount, SkillFxChangeType changeType) : dAmount(amount), m_Sfct(changeType) {}

bool CSkillTickEffect::IsTickEffect() const { return true; }

CSkillTickEffect* CSkillTickEffect::CastTickEffect() { return this; }

double CSkillTickEffect::Amount() { return dAmount; }

CSkillConsumeEffect::CSkillConsumeEffect(double amount) : dAmount(amount) {}

bool CSkillConsumeEffect::IsConsumeEffect() const { return true; }

CSkillConsumeEffect* CSkillConsumeEffect::CastConsumeEffect() { return this; }

double CSkillConsumeEffect::Amount() { return dAmount; }

CSkillInfo::CSkillInfo() :
    m_pszName(nullptr),
    m_nSkillId(0),
    m_nLevel(1),
    m_nSubLevel(0),
    m_nMagicLevel(0),
    m_nIsMagic(0),
    m_nIsHate(0),
    m_nSpecialLevel(0),
    m_bIsEnergyAttack(false),
    m_OperateType(SKILL_MAX),
    m_nReuseDelayGroup(-1),
    m_nNpcNotice(0),
    m_nMpConsume1(0),
    m_nMpConsume2(0),
    m_nMpConsumeTick(0),
    m_nHpConsume(0),
    m_nConsumeItem(0),
    m_nConsumeItemCount(0),
    m_nPledgeNameValueConsume(0),
    m_nPVPPointConsume(0),
    m_eConsumeItemSearchType(ConsumeItemSearchType::CIST_MY_INVEN),
    m_nCastRange(0),
    m_nEffectiveRange(0),
    m_dSkillHitTime(0.0),
    m_dSkillCoolTime(0.0),
    m_dSkillHitCancelTime(0.0),
    m_nReuseDelay(0.0),
    m_bReuseDelayLock(false),
    m_bBlockActionUseSkill(false),
    m_nOlympiadUse(0),
    m_nPledgeGameUse(1),
    m_nReduceCount(-1),
    m_dReduceDamage(-1.0),
    m_nTransferAbnormal(0),
    m_bCondIncludeDead(false),
    m_dLevelBonusRate(0.0),
    m_nActivateRate(0),
    m_nPVPActivateRate(0),
    m_nPVEActivateRate(0),
    m_BasicProperty(static_cast<SetL2ParamEnum>(44)),
    m_nAbnormalTime(0),
    m_nPVPAbnormalTime(0),
    m_nPVEAbnormalTime(0),
    m_nAbnormalLv(0),
    m_bAbnormalTimeCritical(true),
    m_nAbnormalType(ABNS_INVALID),
    m_bAvoidCancelingSkill{},
    m_pAttachedSkill(nullptr),
    m_eAttribute(ATTRT_MAX),
    m_AttributePoint(100),
    m_TraitType(TRAIT_INVALID),
    m_nEffectPoint(0),
    m_IsSummonSkill(false),
    m_IsSummonSiegeSkill(false),
    m_eDebuff(static_cast<DebuffType>(0)),
    m_IrreplaceableBuff(false),
    m_NextAction(static_cast<ActionType>(0)),
    m_bIsFakeDeath(false),
    m_bfRideState(0),
    m_bDeleteBuff(false),
    m_pConsumeEffect(nullptr),
    m_bAttrEnchantType{},
    m_nAfterEffectType(0),
    m_dTickInterval(600.0),
    m_dChannelingStart(0x1.6ed916872b021p+1),
    m_TargetType(STGT_MAX),
    m_AffectScope(SAFS_MAX),
    m_AffectObjectType(static_cast<SkillAffectObjectTypeEnum>(0)),
    m_nAffectScopeMinZ(0),
    m_nAffectScopeMaxZ(0),
    m_nAffectRange(0),
    m_nAffectLimitBase(0),
    m_nAffectLimitRandom(0),
    m_AffectLimitToPC(false),
    m_nCapsuleReduceItemID(-1),
    m_nMultiClass(0),
    m_nEnergy(0),
    m_nSoul(0),
    m_nBuffProtectLevel(0),
    m_bInstantItem(false),
    m_ReuseDelayType(SRDT_NORMAL),
    m_pPrevLevel(nullptr),
    m_pNextLevel(nullptr),
    m_nToggleGroupId(0),
    m_nAttachToggleGroupId(0),
    m_nAttachDoubleCastSkillId(0),
    m_bIsDoubleCasting(false),
    m_IsFlySkill(false),
    m_isClassChangeSkill(false),
    m_bIsNeccessaryTG(true),
    m_bHairAccFlag(false),
    m_bExceptBlock(false),
    m_bExceptRestore(false),
    m_nChangeBuffId(-1),
    m_MagicCriticalRate(0.0),
    m_nSubAbnormal(ABNS_INVALID),
    m_nEventType(0),
    m_bWithoutAction(false),
    m_bUndeletable(false),
    m_nTransformType(0),
    m_DamageProportionType(DamageProportionType::DPT_NOT_APPLIED),
    m_IsOriginAlterSkill(false),
    m_IsAlterSkill(false),
    m_UltimateSkillLv(0),
    m_bHasAwakenPenalty(false),
    m_nAwakenPenaltyLevelBegin(120),
    m_nAwakenPenaltyFactorOnMP(0),
    m_eChargeDirection(SFPT_FRONT),
    m_bSaveToDB(false),
    m_bIsVariationItemEquipReusedelay(false),
    m_bIsAgathionMainSkill(false) {
    L2_GUARD_FUNCTION();
}

CSkillInfo::CSkillInfo(const CSkillInfo& other) = default;

CSkillInfo::~CSkillInfo() = default;

void CSkillInfo::Reset() {
    // Current L2Server 0x00a66af4. The target resets each logical end
    // pointer to begin and keeps vector capacity.
    L2_GUARD_FUNCTION();
    m_vOperateCondForCaster.clear();
    m_vOperateCondForTarget.clear();
    m_vPassiveConditions.clear();
    m_vOperateCondMsgForCaster.clear();
    m_vOperateCondMsgForTarget.clear();
    m_vOperateCondMsgForPassiveConditions.clear();
    m_TickEffects.clear();
    for (std::size_t index = 0; index < EAT_MAX; ++index) {
        m_vPumpEffect[index].clear();
        m_vInstantEffects[index].clear();
    }
    m_vAuraEffect.clear();
    m_vRelatedItems.clear();
}

bool CSkillInfo::IsDurationSkill() const {
    // Current L2Server 0x005e5f68. Keep the explicit accepted set: it is
    // observably narrower than IsActiveSkill and is not a range test.
    switch (m_OperateType) {
        case SKILL_SELFDURATION:
        case SKILL_DURATION:
        case SKILL_CHARGE_DURATION:
        case SKILL_AURA_ACTIVITY:
        case SKILL_AURA_ACTIVITY2:
        case SKILL_DURATION_LEVEL_UP:
        case SKILL_BACK_STEP_DURATION:
        case SKILL_PARTY_INSTANT:
        case SKILL_DUAL_PUMP1:
        case SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK:
            return true;
        default:
            return false;
    }
}

int CSkillInfo::IsMagic() const {
    // Current L2Server 0x005e5f3c. Extended physical/magical spellings are
    // folded to their base categories. All other values remain observable.
    if (m_nIsMagic >= SIMT_PHYSICAL_ATTACK_SKILL && m_nIsMagic <= SIMT_PHYSICAL_ENCHANT_SKILL) {
        return SIMT_PHYSICAL_SKILL;
    }
    if (m_nIsMagic >= SIMT_MAGIC_ELEMENT_SKILL && m_nIsMagic <= SIMT_MAGIC_VOODOO_SKILL) {
        return SIMT_MAGIC_SKILL;
    }
    return m_nIsMagic;
}

void CSkillInfo::CheckChangeSkillInfo() {
    // Current L2Server 0x00a677b4.
    L2_GUARD_FUNCTION();
    if (m_nChangeBuffId < 1) {
        if (m_nChangeBuffId < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] must have [change_skill_id] field",
            //     m_pszName)
            // L2_LOG_ERROR(Log,
            //     L"[%s] must have [change_skill_id] field",
            //     m_pszName)
            L2_LOG_ERROR(
                Log, "Skill '%s' has negative change_skill_id %d; the value is retained.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                m_nChangeBuffId
            );
        }
        return;
    }

    if (g_SkillDB.GetSkillInfo(CSkillKey(m_nChangeBuffId, 1), nullptr) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Invalid Change SKill Id[%d]",
        //     m_pszName,
        //     m_nChangeBuffId)
        // L2_LOG_ERROR(Log,
        //     L"[%s] Invalid Change SKill Id[%d]",
        //     m_pszName,
        //     m_nChangeBuffId)
        L2_LOG_ERROR(
            Log, "Skill '%s' references missing change skill %d level 1 sublevel 0; change_skill_id is retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
            m_nChangeBuffId
        );
    }
}

void CSkillInfo::CheckAttachedDoubleCast() {
    // Current L2Server 0x00a678b4.
    L2_GUARD_FUNCTION();
    if (m_nAttachDoubleCastSkillId == 0) {
        return;
    }

    if (m_bIsDoubleCasting) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Cannot have double cast skill[%d]",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId)
        // L2_LOG_ERROR(Log,
        //     L"[%s] Cannot have double cast skill[%d]",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId)
        L2_LOG_ERROR(
            Log, "Skill '%s' is itself a double-cast skill but attaches skill %d; attachment settings are retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
            m_nAttachDoubleCastSkillId
        );
    }

    const CSkillInfo* const attachedSkill =
        g_SkillDB.GetSkillInfo(CSkillKey(m_nAttachDoubleCastSkillId, m_nLevel, m_nSubLevel), nullptr);
    if (attachedSkill == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Invalid DoubleCastSkill[%d][%d]",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId,
        //     m_nLevel)
        // L2_LOG_ERROR(Log,
        //     L"[%s] Invalid DoubleCastSkill[%d][%d]",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId,
        //     m_nLevel)
        L2_LOG_ERROR(
            Log,
            "Skill '%s': cannot resolve attached double-cast skill %d for requested level %d sublevel %d; attachment "
            "settings are retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
            m_nAttachDoubleCastSkillId, m_nLevel, m_nSubLevel
        );
    } else if (!attachedSkill->m_bIsDoubleCasting) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Invalid DoubleCastSkill[%d] is not double cast skill",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId)
        // L2_LOG_ERROR(Log,
        //     L"[%s] Invalid DoubleCastSkill[%d] is not double cast skill",
        //     m_pszName,
        //     m_nAttachDoubleCastSkillId)
        L2_LOG_ERROR(
            Log, "Skill '%s' attaches skill %d which is not marked as double-cast; attachment settings are retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
            m_nAttachDoubleCastSkillId
        );
    }
}

void CSkillInfo::CheckGroupSkillInfo() {
    // Current L2Server 0x00a67ae8.
    L2_GUARD_FUNCTION();
    if (m_nAttachToggleGroupId < 1) {
        return;
    }

    const auto group = m_mapToggleGroup.find(m_nAttachToggleGroupId);
    if (group == m_mapToggleGroup.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] Invalid ToggleGroupId[%d]",
        //     m_pszName,
        //     m_nAttachToggleGroupId)
        // L2_LOG_ERROR(Log,
        //     L"[%s] Invalid ToggleGroupId[%d]",
        //     m_pszName,
        //     m_nAttachToggleGroupId)
        L2_LOG_ERROR(
            Log, "Skill '%s' references missing toggle group %d; group validation stops and the group ID is retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
            m_nAttachToggleGroupId
        );
        return;
    }

    // The current body copies the group vector before iterating it.
    const std::vector<int> groupSkillIds = group->second;
    for (const int groupSkillId : groupSkillIds) {
        const auto attached = m_mapAttachSkill.find(groupSkillId);
        if (attached == m_mapAttachSkill.end()) {
            continue;
        }

        const int toggleGroupSkillId = attached->first;
        const int attachSkillId = attached->second;
        const CSkillInfo* const toggleGroupSkill = g_SkillDB.GetSkillInfo(CSkillKey(toggleGroupSkillId, 1), nullptr);
        const CSkillInfo* const attachSkill = g_SkillDB.GetSkillInfo(CSkillKey(attachSkillId, 1), nullptr);
        if (toggleGroupSkill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid ToggleGroupSkill[%d]",
            //     m_pszName,
            //     toggleGroupSkillId)
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid ToggleGroupSkill[%d]",
            //     m_pszName,
            //     toggleGroupSkillId)
            L2_LOG_ERROR(
                Log,
                "Skill '%s', toggle group %d: referenced toggle skill %d level 1 sublevel 0 is missing; the attachment "
                "entry is retained.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                m_nAttachToggleGroupId, toggleGroupSkillId
            );
        }
        if (attachSkill == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid AttachSkill[%d]",
            //     m_pszName,
            //     attachSkillId)
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid AttachSkill[%d]",
            //     m_pszName,
            //     attachSkillId)
            L2_LOG_ERROR(
                Log,
                "Skill '%s', toggle group %d: referenced attached skill %d level 1 sublevel 0 is missing; the attachment "
                "entry is retained.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                m_nAttachToggleGroupId, attachSkillId
            );
        }
    }
}

void CSkillInfo::PostLoad() {
    // Current L2Server 0x00a67de0.
    L2_GUARD_FUNCTION();
    const CSkillKey key = GetKey();

    if (m_OperateType == SKILL_CHANNELLING_DURATION || m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP) {
        const auto temp = g_SkillInfoTempDataDB.find(key);
        if (temp == g_SkillInfoTempDataDB.end() || temp->second.m_sAttachedSkillName.empty()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] m_m_sAttachedSkillName is empty()",
            //     m_pszName)
            // L2_LOG_ERROR(Log,
            //     L"[%s] m_m_sAttachedSkillName is empty()",
            //     m_pszName)
            L2_LOG_ERROR(
                Log, "Channelling skill '%s' has no attached-skill name; initialization of this skill stops.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str()
            );
            return;
        }

        const std::wstring attachedName = Utf16ToWide(temp->second.m_sAttachedSkillName.c_str());
        m_pAttachedSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(attachedName.c_str()));
    }

    if (m_nConsumeItemCount > 0) {
        const auto temp = g_SkillInfoTempDataDB.find(key);
        if (temp == g_SkillInfoTempDataDB.end()) {
            // This is the current copy/paste diagnostic, not a correction.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] m_m_sAttachedSkillName is empty()",
            //     m_pszName)
            // L2_LOG_ERROR(Log,
            //     L"[%s] m_m_sAttachedSkillName is empty()",
            //     m_pszName)
            L2_LOG_ERROR(
                Log,
                "Cannot resolve consumed items for skill '%s' (count %lld): temporary item data is missing; initialization "
                "of this skill stops.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                static_cast<long long>(m_nConsumeItemCount)
            );
            return;
        }
        if (temp->second.m_sConsumeItemName.empty()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] m_sConsumeItemName is empty()",
            //     m_pszName)
            // L2_LOG_ERROR(Log,
            //     L"[%s] m_sConsumeItemName is empty()",
            //     m_pszName)
            L2_LOG_ERROR(
                Log,
                "Skill '%s' consumes %lld items but has no item name; item consumption is disabled and initialization of "
                "this skill stops.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                static_cast<long long>(m_nConsumeItemCount)
            );
            m_nConsumeItem = 0;
            m_nConsumeItemCount = 0;
            return;
        }

        m_nConsumeItem = g_ObjectDB.GetClassIdFromName(temp->second.m_sConsumeItemName.c_str(), false);
        if (m_nConsumeItem == -1) {
            const std::string consumeItemName = Utf16ToUtf8(temp->second.m_sConsumeItemName.c_str());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] m_sConsumeItemName isn't in SkilData [%s]",
            //     m_pszName,
            //     consumeItemName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s] m_sConsumeItemName isn't in SkilData [%s]",
            //     m_pszName,
            //     consumeItemName.c_str())
            L2_LOG_ERROR(
                Log,
                "Skill '%s' refers to unknown consumed item '%s' (count %lld); item consumption is disabled and "
                "initialization continues.",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                consumeItemName.c_str(), static_cast<long long>(m_nConsumeItemCount)
            );
            m_nConsumeItem = 0;
            m_nConsumeItemCount = 0;
        }
    }

    if (m_pConsumeEffect != nullptr) {
        m_pConsumeEffect->OnPostLoad();
    }
    SkillEffectsOnPostLoad(m_vAuraEffect);
    SkillEffectsOnPostLoad(m_TickEffects);
    CheckGroupSkillInfo();
    CheckAttachedDoubleCast();
    CheckChangeSkillInfo();
    for (std::size_t index = 0; index < EAT_MAX; ++index) {
        SkillEffectsOnPostLoad(m_vPumpEffect[index]);
        SkillEffectsOnPostLoad(m_vInstantEffects[index]);
    }

    const auto verify = g_SkillInfoVerifyDataDB.find(key);
    if (verify == g_SkillInfoVerifyDataDB.end()) {
        return;
    }

    for (CSkillEffect* const effect : verify->second.m_vEffectList) {
        if (effect == nullptr) {
            continue;
        }
        auto* const callSkillEffect = static_cast<CSkillEffect_i_call_skill*>(effect);
        CSkillInfo* const calledSkill = callSkillEffect->GetCallSkillInfo();
        if (calledSkill == nullptr || calledSkill->m_OperateType != SKILL_DURATION_LEVEL_UP ||
            calledSkill->m_nAbnormalType != m_nAbnormalType) {
            continue;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid script => [%s] id=%d level=%d sub_level=%d AbnormalType=%d effect={i_call_skill.[%s]} - [%s]'s
        //     operate_type is A4 and AbnormalType=%d", m_pszName, m_nSkillId, m_nLevel, m_nSubLevel,
        //     static_cast<int>(m_nAbnormalType),
        //     calledSkill->m_pszName,
        //     calledSkill->m_pszName,
        //     static_cast<int>(calledSkill->m_nAbnormalType))
        // L2_LOG_ERROR(Log,
        //     L"invalid script => [%s] id=%d level=%d sub_level=%d AbnormalType=%d effect={i_call_skill.[%s]} - [%s]'s
        //     operate_type is A4 and AbnormalType=%d", m_pszName, m_nSkillId, m_nLevel, m_nSubLevel,
        //     static_cast<int>(m_nAbnormalType),
        //     calledSkill->m_pszName,
        //     calledSkill->m_pszName,
        //     static_cast<int>(calledSkill->m_nAbnormalType))
        L2_LOG_ERROR(
            Log,
            "Skill '%s' (ID %d level %d sublevel %d) calls A4 skill '%s' with the same abnormal type %d; the effect is "
            "retained.",
            m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(), m_nSkillId,
            m_nLevel, m_nSubLevel,
            calledSkill->m_pszName == nullptr ? "<null>"
                                              : Utf16ToUtf8(target_utf16::FromHostCodeUnits(calledSkill->m_pszName)).c_str(),
            static_cast<int>(m_nAbnormalType)
        );
    }
    verify->second.m_vEffectList.clear();
}

double CSkillInfo::GetConsumeFactor(CCreature* creature) {
    // Current L2Server 0x00a64304.
    L2_GUARD_FUNCTION();
    if (m_nIsMagic == 3 && creature != nullptr) {
        return kMpConsumeBonus[creature->m_nSongDanceCount];
    }
    return 1.0;
}

void CSkillInfo::ShowSkillInfo(User& user) {
    // Current L2Server 0x00a648a0.
    L2_GUARD_FUNCTION();
    char16_t buffer[256]{};
    FormatUtf16To(
        buffer, std::size(buffer), u"skill info. hit_time[%d] hit_cancel_time[%d]", static_cast<int>(m_dSkillHitTime),
        static_cast<int>(m_dSkillHitCancelTime)
    );
    if (CUserSocket* const socket = user.GetSocket()) {
        socket->SendSystemMessage(u"SYS", buffer);
    }
}

const CSkillInfo::SkillEffects* CSkillInfo::GetEffects(EffectActivateTiming timing) const {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(timing);
    return index < EAT_MAX ? &m_vInstantEffects[index] : nullptr;
}

const CSkillInfo::SkillEffects* CSkillInfo::GetPumpEffects(EffectActivateTiming timing) const {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(timing);
    return index < EAT_MAX ? &m_vPumpEffect[index] : nullptr;
}

void CSkillInfo::CollectPumpEffects(std::vector<PumpSkillData>* pumpSkillData) {
    // Current L2Server 0x00a682e0. Both effect lists publish the same
    // source-less 0x18 record. Null effect entries are skipped.
    for (CSkillEffect* effect : m_vPumpEffect[EAT_SUCCESS]) {
        if (effect != nullptr) {
            pumpSkillData->push_back(PumpSkillData{effect, this, 0, 0});
        }
    }
    for (CSkillEffect* effect : m_vAuraEffect) {
        if (effect != nullptr) {
            pumpSkillData->push_back(PumpSkillData{effect, this, 0, 0});
        }
    }
}

double CSkillInfo::GetMesAdjustProb(CCreature* creature) const {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return 1.0;
    }

    if (creature->m_SkillMod.m_AbnormalShield > 0) {
        --creature->m_SkillMod.m_AbnormalShield;
        if (creature->m_SkillMod.m_AbnormalShield == 0) {
            L2_GUARD(creature->m_InfectedSkillsSect);
            creature->_DeleteAbnormalStatus(ABNS_SIGNAL_A, nullptr, nullptr, true, SPET_TIMEOUT, false);
        }
        return 0.0;
    }

    MesType::Enum mesType;
    if (m_eDebuff == DB_PHYSICAL_MES) {
        mesType = MesType::PHYSICAL;
    } else if (m_eDebuff == DB_MENTAL_MES) {
        mesType = MesType::MENTAL;
    } else {
        return 1.0;
    }

    const int count = creature->MesCount(mesType);
    if (count == 0) {
        return 1.0;
    }
    return count == 1 ? 0.6 : 0.3;
}

double CSkillInfo::GetDurationSkillProb(CCreature* creature, CCreature* target) const {
    L2_GUARD_FUNCTION();

    if (m_nActivateRate == -1 || (m_nActivateRate == -2 && target->IsPC()) ||
        (m_nActivateRate == -3 && target->IsNPC() && !target->IsBoss()) || (m_nActivateRate == -4 && !target->IsBoss()) ||
        (m_nPVPActivateRate == -1 && creature->IsPC() && target->IsPC()) ||
        (m_nPVEActivateRate == -1 && creature->IsPC() && !target->IsPC())) {
        return 100.0;
    }

    int basicProperty = 0;
    if (m_BasicProperty != STE_MAX) {
        basicProperty = static_cast<int>(target->GetInt64Param(m_BasicProperty));
    }

    const int magicLevel =
        static_cast<std::uint32_t>(m_nSpecialLevel) + 2U < 2U ? target->m_data->m_nLevel + 3 : m_nMagicLevel;
    const double attributeBonus = creature->GetSkillAttributeBonus(target, this, false);

    int activateRate = m_nActivateRate;
    if (m_nPVPActivateRate > 0 && creature->IsPC() && target->IsPC()) {
        activateRate = m_nPVPActivateRate;
    } else if (m_nPVEActivateRate > 0 && creature->IsPC() && !target->IsPC()) {
        activateRate = m_nPVEActivateRate;
    }

    const int levelDifference = std::min(magicLevel - target->m_data->m_nLevel + 3, 0);
    const double abnormalBonus = m_eDebuff == DB_GOOD ? target->m_SkillMod.m_dGoddBuffAbnormalBonusPer
                                                      : target->m_SkillMod.m_dBadBuffAbnormalBonusPer;
    double probability = (static_cast<double>(levelDifference) * m_dLevelBonusRate + static_cast<double>(activateRate) +
                             30.0 - static_cast<double>(basicProperty)) *
        attributeBonus * abnormalBonus;

    if (probability == 0.0) {
        if (creature->GetGeneralTraitBonus(*target, m_TraitType, false) != 0.0) {
            probability = std::min(probability, 90.0);
        }
    } else if (probability < 0.0) {
        probability = 0.0;
    } else {
        probability = std::min(probability, 90.0);
    }

    if (creature->m_BuilderCmdMod.m_nAbnormalChangeType == 1) {
        probability = 100.0;
    } else if (creature->m_BuilderCmdMod.m_nAbnormalChangeType == 2) {
        probability = 0.0;
    }
    return GetMesAdjustProb(target) * probability;
}

bool CSkillInfo::ActivateDurationSkill(CCreature* creature, CObject* target, bool* reflected) const {
    // Current L2Server 0x00a66df8.
    L2_GUARD_FUNCTION();
    if (target == nullptr || !target->IsCreature()) {
        return false;
    }

    CCreature* effectiveTarget = target->CastCreature();
    if (m_AbnormalVisualEffect.find(21) != m_AbnormalVisualEffect.end() && effectiveTarget->IsSummon() &&
        effectiveTarget->CastSummon()->m_data->m_Race == RT_SIEGE_WEAPON) {
        return false;
    }

    if ((creature->m_nId == effectiveTarget->m_nId && m_eDebuff == DB_GOOD) || m_nActivateRate == -1 ||
        (m_nActivateRate == -2 && effectiveTarget->IsPC()) ||
        (m_nActivateRate == -3 && effectiveTarget->IsNPC() && !effectiveTarget->IsBoss()) ||
        (m_nActivateRate == -4 && !effectiveTarget->IsBoss())) {
        if (m_OperateType == SKILL_DUAL_PUMP1) {
            creature->InfectedSkillBy(effectiveTarget, this, nullptr, 0);
            effectiveTarget->InfectedSkillBy(effectiveTarget, this, nullptr, 0);
        } else {
            effectiveTarget->InfectedSkillBy(creature, this, nullptr, 0);
        }
        return true;
    }

    if (creature->m_nId != effectiveTarget->m_nId &&
        (m_eDebuff == DB_BAD || m_eDebuff == DB_PHYSICAL_MES || m_eDebuff == DB_MENTAL_MES)) {
        int magicType = m_nIsMagic;
        if (magicType >= 10 && magicType < 14) {
            magicType = SIMT_PHYSICAL_SKILL;
        } else if (static_cast<std::uint32_t>(magicType) - 20U < 6U) {
            magicType = SIMT_MAGIC_SKILL;
        }
        if (genrand64_real1() * 100.0 < effectiveTarget->m_SkillMod.m_SkillRefectProb.m_Prob[magicType]) {
            effectiveTarget = creature;
            if (reflected != nullptr) {
                *reflected = true;
            }
        }
    }

    const double probability = GetDurationSkillProb(creature, effectiveTarget);
    if ((genrand64_real1() * 100.0 < probability || creature->m_AlwaysSkillSuccess) &&
        (m_nAbnormalType == ABNS_INVALID || !effectiveTarget->m_SkillMod.m_bAvoidCancelingSkill[m_nAbnormalType])) {
        return effectiveTarget->InfectedSkillBy(creature, this, nullptr, 0);
    }

    if (!effectiveTarget->IsAlive()) {
        return false;
    }
    User* const owner = creature->GetUserOrMaster();
    if (owner == nullptr) {
        return false;
    }

    const int encodedLevel = m_nSubLevel << 16 | static_cast<std::uint16_t>(m_nLevel);
    if (effectiveTarget->IsNPC()) {
        owner->GetSocket()->Send(
            "chccdcdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 2, effectiveTarget->GetClassId(), 4,
            m_nSkillId, encodedLevel
        );
    } else if (effectiveTarget->IsPC()) {
        owner->GetSocket()->Send(
            "chccScdd", 0x62, SystemMessageId::CHARACTER_RESISTED_NAMED_EFFECT, 2, 12,
            std::u16string(effectiveTarget->CastUser()->GetName2(effectiveTarget->CastUser())).c_str(), 4, m_nSkillId,
            encodedLevel
        );
    }
    CCreature::SendMagicAttckInfo(owner, effectiveTarget, MagicAttackInfo::Resistance);
    return false;
}

AlterSkillInfo* CSkillInfo::GetAlterSkillByProb(std::vector<AlterSkillInfo*>& skillList) {
    // Current L2Server 0x00a65f64.
    L2_GUARD_FUNCTION();
    if (skillList.empty()) {
        return nullptr;
    }

    double accumulatedProbability = 0.0;
    for (AlterSkillInfo* const info : skillList) {
        if (info == nullptr) {
            break;
        }
        const double random = genrand64_real1() * (100.0 - accumulatedProbability) + accumulatedProbability;
        accumulatedProbability += info->prob;
        if (random < accumulatedProbability) {
            return info;
        }
    }
    return nullptr;
}

void CSkillInfo::SkillAvoidOrCounter(CCreature& creature, CObject& target, bool& avoided, bool& countered) const {
    // Current L2Server 0x00a638e8.
    L2_GUARD_FUNCTION();
    if (!target.IsCreature() || target.m_nId == creature.m_nId || m_OperateType > SKILL_SELFDURATION ||
        m_nEffectPoint >= 0) {
        return;
    }

    int magicType;
    if (m_nIsMagic == 0 || m_nIsMagic == 6 || (m_nIsMagic >= 10 && m_nIsMagic < 14)) {
        magicType = SIMT_PHYSICAL_SKILL;
    } else if (m_nIsMagic == 1 || m_nIsMagic == 7 || static_cast<std::uint32_t>(m_nIsMagic) - 20U < 6U) {
        magicType = SIMT_MAGIC_SKILL;
    } else {
        return;
    }

    const int counterProbability =
        target.CastCreature()->m_SkillMod.m_SkillReacts[CCreatureSkillMod::CounterSkill].nProb[magicType];
    if (counterProbability > 0 && m_nCastRange >= 0 && m_nCastRange < 41 &&
        genrand64_real1() * 100.0 < creature.GetSkillAttributeBonus(target.CastCreature(), this, false) *
                static_cast<double>(target.CastCreature()->Level() + counterProbability - m_nMagicLevel)) {
        countered = true;
        if (creature.IsPC()) {
            SendSkillReactionMessage(*creature.CastUser(), target, 1997, *this);
        }
        if (target.IsPC()) {
            SendSkillReactionMessage(*target.CastUser(), creature, 1998, *this);
        }
    }

    const int avoidProbability =
        target.CastCreature()->m_SkillMod.m_SkillReacts[CCreatureSkillMod::AvoidSkill].nProb[magicType];
    if (avoidProbability > 0 &&
        genrand64_real1() * 100.0 < creature.GetSkillAttributeBonus(target.CastCreature(), this, false) *
                static_cast<double>(target.CastCreature()->Level() + avoidProbability - m_nMagicLevel)) {
        avoided = true;
        if (target.IsPC()) {
            SendSkillReactionMessage(*target.CastUser(), creature, 2000, *this);
        }
        if (creature.IsPC()) {
            SendSkillReactionMessage(*creature.CastUser(), target, 1999, *this);
        }
    }
}

bool CSkillInfo::CanUsedBy(CCreature* creature, bool infectedSkill) const {
    L2_GUARD_FUNCTION();

    if (!creature->IsPC()) {
        return true;
    }

    if (infectedSkill) {
        const auto debuffType = static_cast<std::uint32_t>(m_eDebuff);
        if (debuffType >= static_cast<std::uint32_t>(DB_BAD) && debuffType <= static_cast<std::uint32_t>(DB_MENTAL_MES)) {
            return true;
        }
    } else if (m_nMultiClass == 1 && creature->m_data->m_nSubJobId != 0) {
        creature->SendSystemMessage(SystemMessageId::SKILL_REQUIRES_MAIN_CLASS);
        return false;
    }

    const auto rideState = static_cast<std::uint32_t>(m_bfRideState);
    const bool unavailableWhileRiding = rideState != 0 && ((rideState & 0x01U) == 0 || creature->IsRiding()) &&
        ((rideState & 0x02U) == 0 || !creature->IsRidingWolf()) &&
        ((rideState & 0x04U) == 0 || !creature->IsRidingStrider()) &&
        ((rideState & 0x08U) == 0 || !creature->IsRidingWyvern());
    if (!unavailableWhileRiding) {
        return true;
    }

    if (creature->IsPC()) {
        CUserSocket* s;
        int m;
        if (!m_IsSummonSkill) {
            s = creature->CastUser()->GetSocket();
            m = SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET;
        } else {
            s = creature->CastUser()->GetSocket();
            m = SystemMessageId::CANNOT_USE_SKILL_NOT_ENOUGH_SUMMON_POINTS;
        }
        const auto l = (std::uint32_t(m_nSubLevel) << 16U) | (std::uint32_t(m_nLevel) & 0xffffU);
        s->Send("chccdd", 0x62, m, 1, 4, m_nSkillId, l);
    }
    return false;
}

bool CSkillInfo::PumpStart(CCreature& creature, CCreature* source, SkillPumpStartType startType) const {
    L2_GUARD_FUNCTION();

    if (source != nullptr && !CanUsedBy(source, false)) {
        return false;
    }
    if ((m_OperateType == SKILL_PASSIVE || m_OperateType == SKILL_RACE_SPECIAL_PASSIVE) &&
        !creature.SetPassiveSkillPumpStart(m_nSkillId, m_nLevel, m_nSubLevel, startType)) {
        return false;
    }

    const SkillEffects* effects = GetPumpEffects(EAT_SUCCESS);
    if (effects == nullptr) {
        return true;
    }
    if (effects->empty()) {
        effects = GetPumpEffects(creature.IsPCOrSummon() ? EAT_PVP : EAT_PVE);
    }

    bool result = true;
    for (CSkillEffect* const effect : *effects) {
        if (effect == nullptr) {
            continue;
        }
        bool conditionsSatisfied = true;
        for (CEffectCondition* const condition : effect->effectCondList) {
            if (!condition->CheckEffectCondition(&creature)) {
                conditionsSatisfied = false;
                break;
            }
        }
        if (conditionsSatisfied) {
            result &= effect->PumpStart(creature, source, this, startType);
        }
    }
    for (CSkillEffect* const effect : m_vAuraEffect) {
        if (effect != nullptr) {
            result &= effect->PumpStart(creature, source, this, startType);
        }
    }
    return result;
}

bool CSkillInfo::PumpEnd(CCreature& creature, CCreature* source, SkillPumpEndType endType) const {
    L2_GUARD_FUNCTION();

    if ((m_OperateType == SKILL_PASSIVE || m_OperateType == SKILL_RACE_SPECIAL_PASSIVE) &&
        !creature.SetPassiveSkillPumpEnd(m_nSkillId, m_nLevel, m_nSubLevel, endType)) {
        return false;
    }

    const SkillEffects* effects = GetPumpEffects(EAT_SUCCESS);
    if (effects != nullptr && effects->empty()) {
        effects = GetPumpEffects(creature.IsPCOrSummon() ? EAT_PVP : EAT_PVE);
    }

    if (effects != nullptr) {
        for (int index = 0; static_cast<std::size_t>(index) < effects->size(); ++index) {
            CSkillEffect* effect = runtime::CheckedVectorAt((*effects), index);
            if (effect != nullptr) {
                effect->PumpEnd(creature, source, this, endType);
            }
        }
    }
    return true;
}

bool CSkillInfo::ActivateAuraSkill(CCreature& creature, SkillTargetTypeEnum targetType) const {
    L2_GUARD_FUNCTION();

    CCreature::SkillTargetData targetData;
    switch (m_TargetType) {
        case STGT_MY_PARTY:
            creature.GatherNeighborParty(
                targetData, m_nAffectRange, m_nAffectScopeMinZ, m_nAffectScopeMaxZ, &creature, false
            );
            break;
        case STGT_MY_PLEDGE:
            creature.GatherNeighborPledge(
                targetData, m_nAffectRange, m_nAffectScopeMinZ, m_nAffectScopeMaxZ, &creature, false
            );
            break;
        case STGT_ENEMY:
            creature.GatherNeighborCreature(
                creature.GetPos(), 1, 0, false, targetData, m_nAffectRange, 0, 0, SAFOT_NOT_FRIEND, &creature, nullptr, true,
                false
            );
            break;
        case STGT_SELF:
            targetData.c.push_back(CSafePointer2<CCreature>(&creature));
            targetData.d.push_back(0.0);
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"m_SkillMod.m_SkillTriggerInfo.eTargetType "
            //     L"[%d] is not valid",
            //     targetType)
            // L2_LOG_ERROR(Log,
            //     L"m_SkillMod.m_SkillTriggerInfo.eTargetType "
            //     L"[%d] is not valid",
            //     targetType)
            L2_LOG_ERROR(
                Log,
                "Cannot activate aura skill '%s' (ID %d level %d sublevel %d): configured target type %d is unsupported "
                "(requested type %d).",
                m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(),
                m_nSkillId, m_nLevel, m_nSubLevel, static_cast<int>(m_TargetType), static_cast<int>(targetType)
            );
            return false;
    }

    for (std::size_t index = 0; index < targetData.c.size(); ++index) {
        CCreature* const target = runtime::CheckedVectorAt(targetData.c, index).FindObjectSP()->CastCreature();
        if (!ActivateSkill(&creature, target, 0.0, nullptr, EAT_SUCCESS, false)) {
            return false;
        }
    }
    return true;
}

bool CSkillInfo::ActivateTriggerSkill(CCreature& creature, CWorldObject* target, SkillTargetTypeEnum targetType) const {
    L2_GUARD_FUNCTION();

    if (targetType == STGT_ENEMY || targetType == STGT_TARGET) {
        if (target == nullptr) {
            return false;
        }
        ActivateSkill(&creature, target, 0.0, nullptr, EAT_SUCCESS, true);
        creature.BroadcastCubicSkillUse(*target, this, 0, 0);
        return true;
    }

    if (targetType == STGT_SELF) {
        ActivateSkill(&creature, &creature, 0.0, nullptr, EAT_SUCCESS, true);
        creature.BroadcastCubicSkillUse(creature, this, 0, 0);
        return true;
    }

    if (targetType == STGT_MY_PARTY) {
        CCreature::SkillTargetData targetData;
        creature.GatherNeighborParty(targetData, 1000, 0, 0, &creature, false);
        for (std::size_t index = 0; index < targetData.c.size(); ++index) {
            CWorldObject* const partyTarget = runtime::CheckedVectorAt(targetData.c, index).FindObjectSP()->CastCreature();
            ActivateSkill(&creature, partyTarget, 0.0, nullptr, EAT_SUCCESS, true);
            creature.BroadcastCubicSkillUse(*partyTarget, this, 0, 0);
        }
        return true;
    }

    if (targetType == STGT_SUMMON) {
        if (!creature.IsPC()) {
            return false;
        }

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            static_cast<User*>(&creature), summon, ActivateSkill(&creature, summon, 0.0, nullptr, EAT_SUCCESS, true);
            creature.BroadcastCubicSkillUse(*summon, this, 0, 0);
        );
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] TriggerSkill[%s]'s eTargetType [%d] is not valid",
    //     L"Skill.cpp",
    //     0xab9,
    //     m_pszName,
    //     targetType)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] TriggerSkill[%s]'s eTargetType [%d] is not valid",
    //     L"Skill.cpp",
    //     0xab9,
    //     m_pszName,
    //     targetType)
    L2_LOG_ERROR(
        Log, "Cannot activate trigger skill '%s' (ID %d level %d sublevel %d): target type %d is unsupported.",
        m_pszName == nullptr ? "<null>" : Utf16ToUtf8(target_utf16::FromHostCodeUnits(m_pszName)).c_str(), m_nSkillId,
        m_nLevel, m_nSubLevel, static_cast<int>(targetType)
    );
    return false;
}

void CSkillInfo::CheckAndTurnOffSoulSpiritShot(CCreature& creature) const {
    L2_GUARD_FUNCTION();

    if ((m_nIsMagic == 0 || m_nIsMagic == 3 || m_nIsMagic == 6) && creature.m_data->m_nSoulShot != 0 &&
        creature.m_sNowSoulShot == 2) {
        creature.TurnOffSoulshot(false);
    }

    if ((m_nIsMagic == 1 || m_nIsMagic == 7) && creature.m_data->m_bSpiritShot) {
        creature.TurnOffSpiritshot(false);
    }
}

void CSkillInfo::SetEnchantAttributeType(AttributeTypeEnum attributeType) {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(attributeType);
    if (index < ATTRT_MAX) {
        m_bAttrEnchantType[index] = true;
    }
}

bool CSkillInfo::GetEnchantAttributeType(AttributeTypeEnum attributeType) {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(attributeType);
    return index < ATTRT_MAX && m_bAttrEnchantType[index];
}

void CSkillInfo::SetRideState(std::vector<std::int64_t>* rideStates) {
    L2_GUARD_FUNCTION();
    auto state = static_cast<std::uint32_t>(m_bfRideState);
    for (const std::int64_t rideState : *rideStates) {
        const auto bit = static_cast<std::uint32_t>(rideState) & 0x1fU;
        state |= std::uint32_t{1} << bit;
        m_bfRideState = static_cast<std::int32_t>(state);
    }
}

bool CSkillInfo::HasAlterSkills() const {
    L2_GUARD_FUNCTION();
    return !m_AlterSkillList.empty();
}

int CSkillInfo::CheckOperateCond(CCreature* creature, CWorldObject* target, OperateConditionTarget conditionTarget) const {
    L2_GUARD_FUNCTION();

    if (conditionTarget == OCT_CASTER || conditionTarget == OCT_BOTH) {
        for (std::size_t index = 0; index < m_vOperateCondForCaster.size(); ++index) {
            if (!runtime::CheckedVectorAt(m_vOperateCondForCaster, index)->CanUse(creature, this, target)) {
                return runtime::CheckedVectorAt(m_vOperateCondMsgForCaster, index);
            }
        }
    }

    if (conditionTarget == OCT_TARGET || conditionTarget == OCT_BOTH) {
        for (std::size_t index = 0; index < m_vOperateCondForTarget.size(); ++index) {
            if (!runtime::CheckedVectorAt(m_vOperateCondForTarget, index)->CanUse(creature, this, target)) {
                return runtime::CheckedVectorAt(m_vOperateCondMsgForTarget, index);
            }
        }
    }

    if (conditionTarget == OCT_PASSIVE) {
        for (std::size_t index = 0; index < m_vPassiveConditions.size(); ++index) {
            if (!runtime::CheckedVectorAt(m_vPassiveConditions, index)->CanUse(creature, this, creature)) {
                return runtime::CheckedVectorAt(m_vOperateCondMsgForPassiveConditions, index);
            }
        }
    }

    return -2;
}

int CSkillInfo::GetNormalReuseDelay(CCreature* creature) const {
    L2_GUARD_FUNCTION();

    if (m_nReuseDelay < 0.0) {
        const runtime::SystemTime local = runtime::GetLocalTimeNow();
        int resetHour = 6;
        int resetMinute = 30;
        if (creature->IsPC()) {
            const int resetTime = creature->CastUser()->GetReuseDelayInitTime();
            resetHour = resetTime / 100;
            resetMinute = resetTime % 100;
        }

        int delaySeconds =
            ((resetHour - static_cast<int>(local.wHour)) * 60 - static_cast<int>(local.wMinute) + resetMinute) * 60 -
            static_cast<int>(local.wSecond);
        if (static_cast<int>(local.wHour) < resetHour ||
            (static_cast<int>(local.wHour) == resetHour && static_cast<int>(local.wMinute) < resetMinute)) {
            delaySeconds += 10;
        } else {
            delaySeconds += 86410;
        }
        return delaySeconds * 1000;
    }

    if (m_bReuseDelayLock) {
        return static_cast<int>(m_nReuseDelay);
    }

    int magicType = m_nIsMagic;
    if (magicType >= 10 && magicType < 14) {
        magicType = SIMT_PHYSICAL_SKILL;
    } else if (static_cast<std::uint32_t>(magicType) - 20U < 6U) {
        magicType = SIMT_MAGIC_SKILL;
    }
    return static_cast<int>(m_nReuseDelay * creature->m_SkillDurationMod.m_dReuseTimeBonusPer[magicType]);
}

void CSkillInfo::SetTransformType(std::vector<int>& transformTypes) {
    L2_GUARD_FUNCTION();
    auto transformType = static_cast<std::uint32_t>(m_nTransformType);
    for (const int value : transformTypes) {
        const auto bit = static_cast<std::uint32_t>(value) & 0x1fU;
        transformType |= std::uint32_t{1} << bit;
        m_nTransformType = static_cast<std::int32_t>(transformType);
    }
}

double CSkillInfo::GetDamageProportionBonus(CCreature& creature) const {
    // Current L2Server 0x00a65a38.
    L2_GUARD_FUNCTION();
    const std::size_t targetCount = std::max<std::size_t>(creature.m_vSkillTarget.c.size(), 1);
    if (m_DamageProportionType == DamageProportionType::DPT_DIRECT) {
        return static_cast<double>(targetCount);
    }
    if (m_DamageProportionType == DamageProportionType::DPT_INVERSE) {
        return 1.0 / static_cast<double>(targetCount);
    }
    return 1.0;
}

bool CSkillInfo::CubicActivate(CCreature* creature, CWorldObject* target, double power) {
    // Current L2Server 0x00a65b70.
    L2_GUARD_FUNCTION();
    (void)power;

    if (target == nullptr) {
        return false;
    }

    if (target->IsNPC() &&
        (target->CastCreature()->m_nClassId == 0x0f761c || target->CastCreature()->m_nClassId == 0x0f761d ||
            target->CastCreature()->m_nClassId == 0x0f761e || target->CastCreature()->m_nClassId == 0x0f742a ||
            target->CastCreature()->m_nClassId == 0x0f742b || target->CastCreature()->m_nClassId == 0x0f7519 ||
            target->CastCreature()->m_nClassId == 0x0f769c || target->CastCreature()->m_nClassId == 0x0f769d ||
            target->CastCreature()->m_nClassId == 0x0f769e)) {
        return false;
    }
    if (target->IsNPC() &&
        (target->CastCreature()->m_nClassId == 0x0f772f || target->CastCreature()->m_nClassId == 0x0f7730)) {
        return false;
    }

    if (CheckOperateCond(creature, nullptr, OCT_CASTER) != -2) {
        return false;
    }

    if (m_OperateType == SKILL_DURATION && target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        const bool unconditional = m_nActivateRate == -1 || (m_nActivateRate == -2 && targetCreature->IsPC()) ||
            (m_nActivateRate == -3 && targetCreature->IsNPC() && !targetCreature->IsBoss()) ||
            (m_nActivateRate == -4 && !targetCreature->IsBoss());

        bool infect = unconditional;
        if (!unconditional) {
            double probability = 120.0;
            if (m_BasicProperty != STE_MAX) {
                const bool useTargetLevel = static_cast<std::uint32_t>(m_nSpecialLevel) + 2U < 2U;
                const int magicLevel = useTargetLevel ? targetCreature->m_data->m_nLevel + 3 : m_nMagicLevel;
                const int levelDifference = std::min(magicLevel - targetCreature->m_data->m_nLevel + 3, 0);
                const int basicProperty = static_cast<int>(targetCreature->GetInt64Param(m_BasicProperty));
                const double attributeBonus = creature->GetSkillAttributeBonus(targetCreature, this, false);
                const double abnormalBonus = m_eDebuff == DB_GOOD ? targetCreature->m_SkillMod.m_dGoddBuffAbnormalBonusPer
                                                                  : targetCreature->m_SkillMod.m_dBadBuffAbnormalBonusPer;
                probability = (static_cast<double>(levelDifference) * m_dLevelBonusRate +
                                  static_cast<double>(m_nActivateRate) + 30.0 - static_cast<double>(basicProperty)) *
                    attributeBonus * abnormalBonus;
                if (probability < 10.0) {
                    probability = 10.0;
                } else if (90.0 < probability) {
                    probability = 90.0;
                }
            }
            infect = probability > ::genrand64_real1() * 100.0 + 0.0;
        }

        if (infect) {
            targetCreature->InfectedSkillBy(creature, this, nullptr, 0);
        }
    }

    ActivateSkillEffects(creature, target, EAT_SUCCESS, nullptr, 0.0);
    return true;
}

int CSkillInfo::GetAttachSkillId(int skillId) const {
    L2_GUARD_FUNCTION();
    const auto found = m_mapAttachSkill.find(skillId);
    return found == m_mapAttachSkill.end() ? 0 : found->second;
}

bool CSkillInfo::PushConditionMsg(int messageId, OperateConditionTarget target) {
    L2_GUARD_FUNCTION();
    switch (target) {
        case OCT_CASTER:
            m_vOperateCondMsgForCaster.push_back(messageId);
            return true;
        case OCT_TARGET:
            m_vOperateCondMsgForTarget.push_back(messageId);
            return true;
        case OCT_PASSIVE:
            m_vOperateCondMsgForPassiveConditions.push_back(messageId);
            return true;
        default:
            return false;
    }
}

void CSkillInfo::PushRelatedItem(int itemId) {
    L2_GUARD_FUNCTION();
    for (const int relatedItem : m_vRelatedItems) {
        if (relatedItem == itemId) {
            return;
        }
    }
    m_vRelatedItems.push_back(itemId);
}

bool CSkillInfo::PushPumpEffect(CSkillEffect* effect, EffectActivateTiming timing) {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(timing);
    if (EAT_MAX <= index) {
        return false;
    }
    m_vPumpEffect[index].push_back(effect);
    return true;
}

bool CSkillInfo::PushEffect(CSkillEffect* effect, EffectActivateTiming timing) {
    L2_GUARD_FUNCTION();
    const auto index = static_cast<std::uint32_t>(timing);
    if (EAT_MAX <= index) {
        return false;
    }
    m_vInstantEffects[index].push_back(effect);
    return true;
}

bool CSkillInfo::PushCondition(CSkillOperateCondition* condition, OperateConditionTarget target) {
    L2_GUARD_FUNCTION();
    switch (target) {
        case OCT_CASTER:
            m_vOperateCondForCaster.push_back(condition);
            return true;
        case OCT_TARGET:
            m_vOperateCondForTarget.push_back(condition);
            return true;
        case OCT_PASSIVE:
            m_vPassiveConditions.push_back(condition);
            return true;
        default:
            return false;
    }
}

bool CSkillInfo::IsCursedRelation(CCreature& creature, CObject& target) const {
    // Current L2Server 0x00a69018.
    L2_GUARD_FUNCTION();

    User* const first = creature.GetUserOrMaster();
    User* const second = target.GetUserOrMaster();
    if (first == nullptr || second == nullptr || !first->IsPC() || !second->IsPC() || first->m_nId == second->m_nId) {
        return false;
    }

    (void)CCursedWeaponMgr::inst();
    const bool firstCursed = first->m_nCursedWeaponClassId > 0;
    (void)CCursedWeaponMgr::inst();
    const bool secondCursed = second->m_nCursedWeaponClassId > 0;
    if (!firstCursed && !secondCursed) {
        return false;
    }
    if (m_nEffectPoint > 0) {
        return true;
    }
    if (firstCursed && second->m_data->m_nLevel < 20) {
        return true;
    }
    return secondCursed && first->m_data->m_nLevel < 20;
}

bool CSkillInfo::IsValidTarget(CCreature* creature, CWorldObject* target, const bool force) const {
    // Current L2Server 0x00a6a2f4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr) {
        return false;
    }

    if (creature->m_AlwaysSkillSuccess) {
        return true;
    }
    if (target->IsNPC()) {
        CNPC* const npc = target->CastNPC();
        if (npc != nullptr && static_cast<int>(npc->m_data->m_nClass) == 0x54) {
            return true;
        }
    }

    if (target->IsCreature() && creature->IsPCOrSummon()) {
        CCreature* const targetCreature = target->CastCreature();
        if (targetCreature->GetPrivateID() != creature->GetPrivateID()) {
            return false;
        }
    }
    if (IsCursedRelation(*creature, *target)) {
        return false;
    }

    if (m_nEffectPoint > 0 && creature->m_nId != target->m_nId && creature->IsInDuel() && target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        User* const user = creature->GetUserOrMaster();
        if (user != nullptr && targetCreature != nullptr) {
            if (user->m_DuelUserInfo.IsInIndividualDuel() && !user->IsDuelEnemy(targetCreature) &&
                !user->IsMySummonOrPet(targetCreature) && user->m_nId != targetCreature->m_nId) {
                return false;
            }
            if (user->m_DuelUserInfo.IsInPartyDuel() && user->IsDuelEnemy(targetCreature)) {
                return false;
            }
        }
    }

    switch (m_TargetType) {
        case STGT_SELF:
        case STGT_GROUND:
        case STGT_EXCEPTION_SELF:
            return creature->m_data->m_bIsAlive != 0 && creature == target;

        case STGT_TARGET:
            return force || !target->IsEnemyTo(creature);

        case STGT_OTHERS:
            return creature != target;

        case STGT_ENEMY:
            if (!target->IsAlive() || creature == target) {
                return false;
            }
            return target->IsDoor() ? target->IsEnemyTo(creature) : force || target->IsEnemyTo(creature);

        case STGT_ENEMY_ONLY:
            if (!target->IsAlive()) {
                return false;
            }
            if (target->IsEnemyTo(creature)) {
                return true;
            }
            if (!force) {
                return false;
            }
            if (User* const first = creature->GetUserOrMaster(); first != nullptr) {
                if (User* const second = target->GetUserOrMaster(); second != nullptr) {
                    return first->CheckDeclareWarBoth(second);
                }
            }
            return false;

        case STGT_ITEM:
            return target->IsItem();

        case STGT_SUMMON:
            return target->IsAlive() && target->IsNPC();

        case STGT_HOLYTHING:
            return target->IsAlive() && target->IsHolyThing();

        case STGT_MY_PLEDGE: {
            if (!creature->IsPC() || !target->IsPC()) {
                return false;
            }
            User* const sourceUser = creature->CastUser();
            return sourceUser->m_data->m_nPledgeID != 0 &&
                sourceUser->m_data->m_nPledgeID == target->CastUser()->m_data->m_nPledgeID;
        }

        case STGT_DOOR_TREASURE:
            if (target->IsNPC()) {
                CNPC* const npc = target->CastNPC();
                if (npc != nullptr && static_cast<int>(npc->m_data->m_nClass) == 0x53) {
                    return true;
                }
            }
            if (!target->IsDoor()) {
                return false;
            }
            return !target->CastDoor()->m_data->m_bIsOpen;

        case STGT_PC_DEAD_BODY:
            if ((!target->IsPC() && !target->IsPet()) || target->IsAlive()) {
                return false;
            }
            return force || !target->IsEnemyTo(creature);

        case STGT_NPC_DEAD_BODY:
            if (!target->IsNPC() || target->IsPet()) {
                return false;
            }
            return !target->CastNPC()->IsAlive();

        case STGT_WYVERN_TARGET:
            if (creature == target || !creature->IsPC() || !creature->IsRidingWyvern() || !target->IsPC()) {
                return false;
            }
            if (!target->CastCreature()->IsAlive()) {
                return false;
            }
            return force || target->IsEnemyTo(creature);

        case STGT_ARTILLERY:
            return creature->IsAlive() && creature->IsSummonOrPet() && creature == target;

        case STGT_FORTRESS_FLAGPOLE:
            return target->IsAlive() && target->IsFortressFlagpole();

        case STGT_SHIP_CANNON:
            return creature->IsAlive() && creature->IsNPC() && creature == target;

        case STGT_ADVANCE_BASE:
            return target->IsAlive() && target->IsAdvanceBase();

        case STGT_ENEMY_NOT:
            return target->IsAlive() && !target->IsEnemyTo(creature);

        case STGT_MY_MENTOR:
            if (!creature->IsPC() || !target->IsPC()) {
                return false;
            }
            return creature->CastUser()->IsMyMentor(target->CastUser());

        default:
            return false;
    }
}

bool CSkillInfo::CanActivateSkill(CCreature* creature, CObject* target) const {
    // Current L2Server 0x00a69188.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || target == nullptr) {
        return false;
    }

    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (static_cast<int>(targetCreature->m_data->m_nClass) == 0x54) {
            return false;
        }
    }

    if (target->IsNPC()) {
        if (target->CastCreature()->m_nClassId == 0x0f761c || target->CastCreature()->m_nClassId == 0x0f761d ||
            target->CastCreature()->m_nClassId == 0x0f761e || target->CastCreature()->m_nClassId == 0x0f742a ||
            target->CastCreature()->m_nClassId == 0x0f742b || target->CastCreature()->m_nClassId == 0x0f7519 ||
            target->CastCreature()->m_nClassId == 0x0f769c || target->CastCreature()->m_nClassId == 0x0f769d ||
            target->CastCreature()->m_nClassId == 0x0f769e) {
            return false;
        }
    }

    if (IsCursedRelation(*creature, *target)) {
        return false;
    }

    if (target->IsPC()) {
        User* const targetUser = target->CastUser();
        if (CMiniGameAgit::IsPlayer(targetUser) && creature->IsPC()) {
            User* const actorUser = creature->CastUser();
            if (!CMiniGameAgit::IsPlayer(actorUser)) {
                return false;
            }
        }
    }

    if (m_nEffectPoint > 0 && creature->IsPC()) {
        if (creature->CastUser()->m_PledgeGameUser.IsPlayer() && target->IsCreature()) {
            if (!creature->CastUser()->IsMySummonOrPet(target->CastCreature()) &&
                creature->m_nId != target->CastCreature()->m_nId) {
                return false;
            }
        }
    }

    if (creature->IsInDuel() && target->IsCreature() && creature->m_nId != target->m_nId) {
        User* const actorUser = creature->GetUserOrMaster();
        CCreature* const targetCreature = target->CastCreature();
        if (actorUser != nullptr && targetCreature != nullptr && m_nEffectPoint > 0) {
            if (actorUser->m_DuelUserInfo.IsInIndividualDuel() && !actorUser->IsDuelEnemy(targetCreature) &&
                !actorUser->IsMySummonOrPet(targetCreature) && actorUser->m_nId != targetCreature->m_nId) {
                return false;
            }
            if (actorUser->m_DuelUserInfo.IsInPartyDuel() && actorUser->IsDuelEnemy(targetCreature)) {
                return false;
            }
        }
    }
    return true;
}

bool CSkillInfo::CheckActivateSkillEffects(CCreature* creature, CObject* target, EffectActivateTiming timing) const {
    // Current L2Server 0x00a6952c.
    L2_GUARD_FUNCTION();
    if (timing != EAT_CANCEL && timing != EAT_END && timing != EAT_SELF) {
        return false;
    }
    if (!CanActivateSkill(creature, target)) {
        return false;
    }
    const SkillEffects* const effects = GetEffects(timing);
    return effects != nullptr && !effects->empty();
}

void CSkillInfo::PostActivateSkillEffects(CCreature* creature, CWorldObject* target, EffectActivateTiming timing) const {
    // Current L2Server 0x00a6961c.
    L2_GUARD_FUNCTION();
    if (CheckActivateSkillEffects(creature, target, timing)) {
        creature->PushSerializedMessage(new CSerializedActivateSkillEffectsMessage(creature, target, *this, timing));
    }
}

void CSkillInfo::SetAvoidSkillCanceling(CCreature& creature, const bool enabled) const {
    // Current L2Server 0x00a65850.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    for (std::size_t index = 0; index < ABNS_MAX; ++index) {
        if (m_bAvoidCancelingSkill[index]) {
            creature.m_SkillMod.m_bAvoidCancelingSkill[index] = enabled;
        }
    }
}

void CSkillInfo::ActivateSkillEffects(
    CCreature* creature, CWorldObject* target, EffectActivateTiming timing, CSkillAction2* action, double distance
) const {
    // Current L2Server 0x00a651dc.
    L2_GUARD_FUNCTION();
    const SkillEffects* const effects = GetEffects(timing);

    if (creature == nullptr) {
        for (CSkillEffect* const effect : *effects) {
            effect->InstantByArea(target, this);
        }
        return;
    }

    if (m_nConsumeItem != 0) {
        CObject* const object = g_ObjectDB.GetObjectW(m_nConsumeItem, false);
        if (object == nullptr) {
            return;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (item != nullptr && item->m_const != nullptr &&
            item->m_const->m_enumDefaultAction == ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS) {
            User* const user = creature->CastUser();
            if (user == nullptr) {
                return;
            }
            if (!user->m_inv.HaveItemByClassId(m_nConsumeItem, m_nConsumeItemCount, false, false)) {
                user->SendSystemMessage(SystemMessageId::CANNOT_USE_SKILL_REQUIRED_ITEMS_MISSING);
                return;
            }
            if (timing == EAT_SUCCESS && !item->CheckItemUseCondition(creature)) {
                user->GetSocket()->Send(
                    "chccd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, 3, item->GetClassId()
                );
                if (!user->m_ItemMasterBuilder) {
                    return;
                }
                user->GetSocket()->SendSystemMessage(u"SYS", u"use item because you are Item Master");
            }
        }
    }

    bool succeeded = false;
    for (CSkillEffect* const effect : *effects) {
        succeeded = effect->Instant(creature, target, this, action, distance);
    }

    if (timing == EAT_SUCCESS) {
        if (action != nullptr && succeeded) {
            action->OnInstantEffectSuccess(creature);
        }
        if (target != nullptr) {
            ActivateSkillEffects(creature, target, target->IsPvpTo(creature) ? EAT_PVP : EAT_PVE, action, distance);
        }
    }
}

bool CSkillInfo::ActivateSkill(
    CCreature* creature,
    CWorldObject* target,
    double distance,
    CSkillAction2* action,
    EffectActivateTiming timing,
    bool durationRequired
) const {
    // Current L2Server 0x00a69724.
    L2_GUARD_FUNCTION();

    if ((timing != EAT_SUCCESS && timing != EAT_START && timing != EAT_TICK) || !CanActivateSkill(creature, target)) {
        return false;
    }

    if (target->IsCreature()) {
        CCreature* const targetCreature = target->CastCreature();
        if (targetCreature->IsIgnoreSkill(m_nSkillId)) {
            return false;
        }
    }

    if (m_OperateType == SKILL_CHANNELLING_DURATION || m_OperateType == SKILL_CHANNELLING_DURATION_LEVEL_UP) {
        return target->CastCreature()->AttachedAbnormalStatusByChannelling(creature, const_cast<CSkillInfo*>(this), target);
    }

    const SkillEffects* effects = GetEffects(timing);
    if (effects == nullptr || (timing > EAT_START && effects->empty())) {
        return true;
    }

    if (creature->IsInDuel() && target->IsCreature() && creature->m_nId != target->m_nId) {
        User* const user = creature->GetUserOrMaster();
        CCreature* const targetCreature = target->CastCreature();
        if (user != nullptr && targetCreature != nullptr && targetCreature->IsNPC() && !targetCreature->IsSummonOrPet() &&
            !targetCreature->IsSummonedNpc() && user->m_DuelUserInfo.IsInIndividualDuel() &&
            user->m_DuelUserInfo.IsInFightStatus()) {
            CDuelManager::inst().CancelDuel(user, 0);
        }
    }

    CNPC* targetNpc = nullptr;
    if (m_nEffectPoint < 0 ||
        (target->IsCreature() && target->IsNPC() && (targetNpc = target->CastNPC()) != nullptr &&
            targetNpc->IsEnemyTo(creature))) {
        SetAsCombatMode(creature, target);
    }

    std::array<bool, 10> summonEligible{};
    std::vector<CSummon*> summons;
    if (m_eDebuff == DB_GOOD && m_nTransferAbnormal == 0 && m_nAbnormalType != ABNS_ABILITY_CHANGE && target->IsPC()) {
        User* const targetUser = target->CastUser();
        targetUser->GetAllSummonAndPet(summons);
        if (m_OperateType != SKILL_AURA_ACTIVITY && m_OperateType != SKILL_AURA_ACTIVITY2) {
            std::size_t summonIndex = 0;
            int summonCount = 0;
            for (CSummon* const summon : summons) {
                if (summon == nullptr) {
                    continue;
                }
                if (summonIndex >= summonEligible.size()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"summon array exceeds max! %d/%d [%s]",
                    //     summonCount,
                    //     10,
                    //     targetUser->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"summon array exceeds max! %d/%d [%s]",
                    //     summonCount,
                    //     10,
                    //     targetUser->GetName())
                    // L2_LOG_ERROR(
                    //     Log,
                    //     "Skill ID %d level %d sublevel %d stopped for user '%s': summon eligibility capacity %llu exceeded
                    //     " "after %d non-null summons or pets.", m_nSkillId, m_nLevel, m_nSubLevel,
                    //     WideToUtf8(targetUser->GetName()).c_str(), static_cast<unsigned long long>(summonEligible.size()),
                    //     summonCount
                    // )
                    L2_LOG_ERROR(
                        Log,
                        "Skill ID %d level %d sublevel %d stopped for user '%s': summon eligibility capacity %llu exceeded "
                        "after %d non-null summons or pets.",
                        m_nSkillId, m_nLevel, m_nSubLevel, Utf16ToUtf8(targetUser->GetName()).c_str(),
                        static_cast<unsigned long long>(summonEligible.size()), summonCount
                    );
                    return false;
                }
                if (!summon->IsIgnoreSkill(m_nSkillId) && m_nAbnormalType != ABNS_ABILITY_CHANGE &&
                    CheckOperateCond(creature, summon, OCT_BOTH) == -2) {
                    summonEligible[summonIndex] = CanActivateSkill(creature, summon);
                }
                ++summonCount;
                ++summonIndex;
            }
        }
    }

    bool reflected = false;
    bool infected = false;
    switch (m_OperateType) {
        case SKILL_DURATION:
        case SKILL_CHARGE_DURATION:
        case SKILL_AURA_ACTIVITY:
        case SKILL_BACK_STEP_DURATION:
        case SKILL_AURA_ACTIVITY2:
        case SKILL_PARTY_INSTANT:
        case SKILL_DUAL_PUMP1: {
            infected = ActivateDurationSkill(creature, target, &reflected);
            std::size_t summonIndex = 0;
            for (CSummon* const summon : summons) {
                if (summon != nullptr) {
                    if (summonEligible[summonIndex]) {
                        ActivateDurationSkill(creature, summon, nullptr);
                    }
                    ++summonIndex;
                }
            }
            if (target->IsPC() && infected) {
                target->CastUser()->TriggerAlterSkill(AlterSkill::Abnormal, m_nAbnormalType);
            }
            break;
        }
        default:
            break;
    }

    if (m_AbnormalVisualEffect.find(21) != m_AbnormalVisualEffect.end() && target->IsPC()) {
        CPet* const pet = target->CastUser()->GetPet();
        if (pet != nullptr && CheckOperateCond(creature, pet, OCT_BOTH) != 0 && CanActivateSkill(creature, pet)) {
            ActivateDurationSkill(creature, pet, nullptr);
        }
    }

    if (m_OperateType == SKILL_SELFDURATION) {
        infected = ActivateDurationSkill(creature, creature, nullptr);
    }

    const CSkillInfo* activatedSkill = this;
    if (m_OperateType == SKILL_DURATION_LEVEL_UP) {
        if (target->IsCreature()) {
            CCreature* const targetCreature = target->CastCreature();
            L2_GUARD(targetCreature->m_InfectedSkillsSect);
            bool differentInstantItem = false;
            int buffCount = 0;
            int debuffCount = 0;
            int songDanceCount = 0;
            int triggerCount = 0;
            const int sameIndex = targetCreature->_FindSameBuff(
                this, differentInstantItem, buffCount, debuffCount, songDanceCount, triggerCount, targetCreature
            );
            if (sameIndex >= 0) {
                activatedSkill =
                    runtime::CheckedVectorAt(targetCreature->m_InfectedSkills, static_cast<std::size_t>(sameIndex)).pSkillInfo;
                if (activatedSkill->m_pNextLevel != nullptr) {
                    activatedSkill = activatedSkill->m_pNextLevel;
                }
            }
        }

        infected = activatedSkill->ActivateDurationSkill(creature, target, nullptr);
        std::size_t summonIndex = 0;
        for (CSummon* const summon : summons) {
            if (summon != nullptr && summonEligible[summonIndex]) {
                activatedSkill->ActivateDurationSkill(creature, summon, nullptr);
            }
            ++summonIndex;
        }
        if (target->IsPC()) {
            CPet* const pet = target->CastUser()->GetPet();
            if (pet != nullptr && CheckOperateCond(creature, pet, OCT_BOTH) != 0 && CanActivateSkill(creature, pet) &&
                activatedSkill->m_eDebuff == DB_GOOD) {
                activatedSkill->ActivateDurationSkill(creature, pet, nullptr);
            }
        }
    }

    bool durationOperateType = false;
    switch (m_OperateType) {
        case SKILL_DURATION:
        case SKILL_DURATION_LEVEL_UP:
        case SKILL_SELFDURATION:
        case SKILL_CHARGE_DURATION:
        case SKILL_AURA_ACTIVITY:
        case SKILL_BACK_STEP_DURATION:
        case SKILL_AURA_ACTIVITY2:
        case SKILL_PARTY_INSTANT:
        case SKILL_DUAL_PUMP1:
        case SKILL_BACK_STEP_DURATION_WITH_TARGET_KNOCKBACK:
            durationOperateType = true;
            break;
        default:
            break;
    }
    if (durationRequired && !infected && durationOperateType &&
        (m_eDebuff == DB_BAD || m_eDebuff == DB_PHYSICAL_MES || m_eDebuff == DB_MENTAL_MES)) {
        return false;
    }

    if (action != nullptr) {
        action->m_Infected = infected;
    }

    bool avoided = false;
    bool countered = false;
    activatedSkill->SkillAvoidOrCounter(*creature, *target, avoided, countered);
    if (!avoided) {
        if (!reflected) {
            activatedSkill->ActivateSkillEffects(creature, target, timing, action, distance);
        }

        if (countered && target->IsCreature()) {
            const SkillEffects* counterEffects = effects;
            if (counterEffects->empty()) {
                counterEffects = GetEffects(target->IsPC() ? EAT_PVP : EAT_PVE);
            }
            if (counterEffects != nullptr && !counterEffects->empty()) {
                CCreature* const targetCreature = target->CastCreature();
                for (std::size_t index = 0; index < counterEffects->size(); ++index) {
                    const double weaponTraitBonus = targetCreature->GetWeaponTraitBonus(creature->m_data->m_AttackType);
                    const double targetPDefend = targetCreature->m_data->m_dParam[VALIDATE::PDEFEND];
                    const double creaturePAttack = creature->m_data->m_dParam[VALIDATE::PATTACK];
                    const double attributeBonus = creature->GetSkillAttributeBonus(targetCreature, this, true);
                    const double damage =
                        attributeBonus * weaponTraitBonus * ((targetPDefend * 10.0 * 70.0) / creaturePAttack);
                    creature->GotDamageBy(targetCreature, damage, false, DAMAGE_NORMAL, true);
                }
            }
        }
    }
    return true;
}

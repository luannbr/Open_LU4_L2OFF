#include "Cubic.h"

#include "Item.h"
#include "ObjectDB.h"
#include "Packet.h"
#include "ParserCubic.h"
#include "Skill.h"
#include "StaticObject.h"
#include "User.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

CSprite::CSprite(int classId, int level, int slot) : m_classID(classId), m_level(level), m_slot(slot) {}

CSprite* CSprite::CastSprite() { return this; }

CAgathion* CSprite::CastAgathion() { return nullptr; }

CCubic* CSprite::CastCubic() { return nullptr; }

CCubic::CCubic(int classId, int level, int slot, int duration, int delay, int maxCount, double power) :
    CSprite(classId, level, slot),
    m_duration(duration),
    m_delay(delay),
    m_maxCount(maxCount),
    m_power(power),
    m_maxActivateProb(0),
    m_UseUp(false),
    m_opCond{},
    m_skillList{},
    m_CubicTimeSkillCount(0) {
    for (CubicSkillInfo& skill : m_CubicTimeSkills) {
        skill.m_ActivateProb = 0;
        skill.m_pSkillInfo = nullptr;
        skill.m_HitProb = 0;
        skill.m_bAttackStaticObject = false;
        skill.m_TargetType = static_cast<SpriteTargetType>(0);
        skill.m_OpCond.op = static_cast<SpriteSkillOp>(0);
        skill.m_OpCond.hpRatio = 0;
        skill.m_OpCond.hpAbs = 0;
        skill.m_CycleSec = 0;
        skill.m_eTimeSkillType = static_cast<CubicTimeSkillType>(0);
    }
}

CCubic* CCubic::CastCubic() { return this; }

void CCubic::SetNPCClassID(int) {}

CAgathion::CAgathion(int classId, int level, int slot, int duration, int delay, int maxCount, double power) :
    CCubic(classId, level, slot, duration, delay, maxCount, power), m_npcClassID(0) {}

CAgathion* CAgathion::CastAgathion() { return this; }

void CAgathion::SetNPCClassID(int npcClassId) { m_npcClassID = npcClassId; }

bool CCubic::PushTimeSkill(const CubicSkillInfo& skillInfo) {
    L2_GUARD_FUNCTION();

    if (m_CubicTimeSkillCount >= 5 || skillInfo.m_pSkillInfo == nullptr) {
        return false;
    }

    CubicSkillInfo& destination = m_CubicTimeSkills[m_CubicTimeSkillCount];
    std::memcpy(&destination, &skillInfo, sizeof(destination));
    destination.m_ActivateProb = 100;
    destination.m_HitProb = 100;
    ++m_CubicTimeSkillCount;
    return true;
}

int CCubic::TimeSkillDelayShow(User& user, CubicActivationInfo& activationInfo) {
    L2_GUARD_FUNCTION();

    int totalRemainingTime = 0;
    const std::int32_t now = runtime::crt::Time32Now();
    for (int index = 0; index < m_CubicTimeSkillCount; ++index) {
        const CubicSkillInfo& skill = m_CubicTimeSkills[index];
        int remainingTime = runtime::AddInt32Wrapping(
            runtime::SubtractInt32Wrapping(skill.m_CycleSec, now), activationInfo.m_TimeSkillLaunched[index]
        );
        if (remainingTime < 0) {
            remainingTime = 0;
        }
        if (skill.m_pSkillInfo != nullptr) {
            user.SendRemainTime(
                remainingTime, EST_SKILLID, skill.m_pSkillInfo->m_nSkillId, skill.m_pSkillInfo->m_nLevel,
                skill.m_pSkillInfo->m_nSubLevel, 0
            );
        }
        totalRemainingTime = runtime::AddInt32Wrapping(totalRemainingTime, remainingTime);
    }
    return totalRemainingTime;
}

void CCubic::TimeSkillDelayDel(User&, CubicActivationInfo& activationInfo, const int elapsedTime) {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < m_CubicTimeSkillCount; ++index) {
        activationInfo.m_TimeSkillLaunched[index] =
            runtime::SubtractInt32Wrapping(activationInfo.m_TimeSkillLaunched[index], elapsedTime);
    }
}

void CCubic::TimeSkillDelayReset(User&, CubicActivationInfo& activationInfo) {
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    for (int index = 0; index < m_CubicTimeSkillCount; ++index) {
        activationInfo.m_TimeSkillLaunched[index] = now;
    }
}

void CCubic::SetElapsedTimeToDailyTimeSkill(User* user, int elapsedTime, CubicActivationInfo* activationInfo) {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < m_CubicTimeSkillCount; ++index) {
        CubicSkillInfo& skill = m_CubicTimeSkills[index];
        if (skill.m_eTimeSkillType == CTST_DAILY) {
            activationInfo->m_TimeSkillLaunched[index] =
                runtime::SubtractInt32Wrapping(activationInfo->m_TimeSkillLaunched[index], elapsedTime);
            if (user->GetRemainReuseDelaySec(
                    skill.m_pSkillInfo->m_nSkillId, skill.m_pSkillInfo->m_nLevel, skill.m_pSkillInfo->m_nSubLevel
                ) > 0.0) {
                activationInfo->m_bResetDailySkills[index] = false;
            }
        }
    }
}

void CCubic::RemoveDailyTimeSkillDelay(User* user) {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < m_CubicTimeSkillCount; ++index) {
        CubicSkillInfo& skill = m_CubicTimeSkills[index];
        if (skill.m_eTimeSkillType == CTST_DAILY) {
            user->RemoveSkillUsableTime(skill.m_pSkillInfo->m_nSkillId);
        }
    }
}

bool CCubic::SetEnergy(int energy) {
    L2_GUARD_FUNCTION();

    if (m_nItemClassIds.empty()) {
        return false;
    }
    for (const int itemClassId : m_nItemClassIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr) {
            return false;
        }
        static_cast<CItem*>(object)->m_const->m_dAgathionEnergy = static_cast<double>(energy);
    }
    return true;
}

bool CCubic::SetMaxEnergy(int maxEnergy) {
    L2_GUARD_FUNCTION();

    if (m_nItemClassIds.empty()) {
        return false;
    }
    for (const int itemClassId : m_nItemClassIds) {
        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr) {
            return false;
        }
        static_cast<CItem*>(object)->m_const->m_dAgathionMaxEnergy = static_cast<double>(maxEnergy);
    }
    return true;
}

void CCubic::AddItemId(int itemClassId) {
    L2_GUARD_FUNCTION();

    if (std::find(m_nItemClassIds.begin(), m_nItemClassIds.end(), itemClassId) == m_nItemClassIds.end()) {
        m_nItemClassIds.push_back(itemClassId);
    }
}

SpriteDB& SpriteDB::Inst() {
    static SpriteDB inst;
    return inst;
}

CSprite* SpriteDB::GetInfo(SpriteKeyType key) {
    L2_GUARD_FUNCTION();

    const auto position = m_spriteMap.find(key);
    if (position == m_spriteMap.end()) {
        if (key.second.second != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Unregistered Sprite(%d) [%d-%d]",
            //     L"Cubic.cpp",
            //     0x3d0,
            //     static_cast<int>(key.first),
            //     key.second.first,
            //     key.second.second)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Unregistered Sprite(%d) [%d-%d]",
            //     L"Cubic.cpp",
            //     0x3d0,
            //     static_cast<int>(key.first),
            //     key.second.first,
            //     key.second.second)
            L2_LOG_ERROR(
                Log, "Sprite lookup failed for type %d, class ID %d, level %d; returning no sprite.",
                static_cast<int>(key.first), key.second.first, key.second.second
            );
        }
        return nullptr;
    }
    return position->second;
}

void SpriteDB::Add(SpriteType type, CSprite* sprite, int classId, int level) {
    L2_GUARD_FUNCTION();

    const SpriteKeyType key{type, SummonSkillType{classId, level}};
    const auto position = m_spriteMap.find(key);
    if (position == m_spriteMap.end()) {
        m_spriteMap.emplace(key, sprite);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Duplicated Sprite(%d) [%d-%d]",
    //     L"Cubic.cpp",
    //     0x3e2,
    //     static_cast<int>(type),
    //     classId,
    //     level)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Duplicated Sprite(%d) [%d-%d]",
    //     L"Cubic.cpp",
    //     0x3e2,
    //     static_cast<int>(type),
    //     classId,
    //     level)
    L2_LOG_ERROR(
        Log, "Duplicate sprite type %d, class ID %d, level %d ignored; registered entry retained.", static_cast<int>(type),
        classId, level
    );
}

bool SpriteDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kCubicDataPath = L"..\\Script\\CubicData.txt";
#else
    constexpr const wchar_t* kCubicDataPath = L"../script/CubicData.txt";
#endif

    CSecureStream input(kCubicDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from CubicData file failed",
        //     L"Cubic.cpp",
        //     0x42a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from CubicData file failed",
        //     L"Cubic.cpp",
        //     0x42a)
        L2_LOG_ERROR(Log, "Cannot read CubicData.txt; sprite loading stops.");
        return false;
    }
    return LoadData(input.GetRawString() + 1, input.GetRawStringLen() - 1);
}

bool SpriteDB::LoadData(char16_t* data, int dataLength) {
    L2_GUARD_FUNCTION();

    CParserForCubic parser;
    if (!parser.Create(data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CubicParser.create failed",
        //     L"Cubic.cpp",
        //     0x43c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CubicParser.create failed",
        //     L"Cubic.cpp",
        //     0x43c)
        L2_LOG_ERROR(Log, "Cannot initialize CubicData.txt parser; sprite loading stops.");
        return false;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CubicData file failed after line[%d]",
        //     L"Cubic.cpp",
        //     0x448,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CubicData file failed after line[%d]",
        //     L"Cubic.cpp",
        //     0x448,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse CubicData.txt at script line %lld; sprites registered before the error are retained and loading "
            "returns false.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        return false;
    }
    return true;
}

bool CCubicHandler::CheckOpCond(CObject* object, const double& currentHp, const double& maximumHp, CubicOpCond& condition) {
    // Current L2Server 0x00638038.
    L2_GUARD_FUNCTION();

    switch (condition.op) {
        case SSO_OVER:
            if (currentHp <= static_cast<double>(condition.hpAbs) &&
                currentHp <= static_cast<double>(condition.hpRatio) * maximumHp / 100.0) {
                return false;
            }
            return true;
        case SSO_BELOW:
            if (static_cast<double>(condition.hpAbs) <= currentHp &&
                static_cast<double>(condition.hpRatio) * maximumHp / 100.0 <= currentHp) {
                return false;
            }
            return true;
        case SSO_DEBUFF: {
            if (object == nullptr) {
                return false;
            }
            CCreature* const creature = object->CastCreature();
            return creature != nullptr && creature->HasAbnormalCategory(DB_BAD);
        }
        default:
            return true;
    }
}

bool CCubicHandler::CanUseSkill(CSkillInfo* skillInfo, CCreature* creature, CWorldObject* target) {
    // Current L2Server 0x00638194.
    L2_GUARD_FUNCTION();

    if (skillInfo->m_nEffectPoint < 0 && !creature->CanUseBadSkill(target, false)) {
        return false;
    }
    return skillInfo->IsValidTarget(creature, target, false);
}

bool CCubicHandler::TryToUseSkill(
    CCreature* creature,
    CWorldObject* target,
    CCubic* cubic,
    CubicActivationInfo& activationInfo,
    ActivateInfo2& activateInfo,
    CubicSkillInfo* cubicSkillInfo
) {
    // Current L2Server 0x0063827c.
    L2_GUARD_FUNCTION();

    if (target == nullptr || cubicSkillInfo->m_pSkillInfo == nullptr) {
        return false;
    }

    const bool bypassCombatMode = cubic->m_targetType == STT_MASTER ||
        (cubic->m_targetType == STT_BY_SKILL && cubicSkillInfo->m_TargetType == STT_MASTER);
    if (!bypassCombatMode && !creature->m_data->m_bIsInCombatMode) {
        return false;
    }

    double currentHp = 0.0;
    double maximumHp = 0.0;
    if (!target->IsCreature()) {
        if (!cubicSkillInfo->m_bAttackStaticObject || !target->IsStaticObject()) {
            return false;
        }
        currentHp = target->CastStaticObject()->m_data->m_dHP;
        maximumHp = target->CastStaticObject()->m_data->m_dMaxHP;
    } else {
        if (!bypassCombatMode && !target->CastCreature()->m_data->m_bIsInCombatMode) {
            return false;
        }
        currentHp = target->CastCreature()->m_data->m_dHP;
        maximumHp = target->CastCreature()->m_data->m_dBaseHP;
    }

    if (!CheckOpCond(target, currentHp, maximumHp, cubic->m_opCond) ||
        !CheckOpCond(target, currentHp, maximumHp, cubicSkillInfo->m_OpCond)) {
        return false;
    }

    CSkillInfo* const skillInfo = cubicSkillInfo->m_pSkillInfo;
    if (!CanUseSkill(skillInfo, creature, target) ||
        !(::genrand64_real1() * 100.0 + 0.0 < static_cast<double>(cubicSkillInfo->m_HitProb))) {
        return false;
    }

    activateInfo.m_pSkillInfo = skillInfo;
    activateInfo.m_TargetSID = target;
    activateInfo.m_NeedCheckTarget = true;
    activateInfo.m_CountDown = 3;
    (void)activationInfo;
    return true;
}

CubicSkillInfo* CCubicHandler::ChooseSkill(
    CCreature& creature, CCubic& cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x00638c98.
    L2_GUARD_FUNCTION();

    if (!activationInfo.m_bTimeSkill) {
        const int roll = runtime::Win64CvttDoubleToInt32(
            ::genrand64_real2() * static_cast<double>(runtime::AddInt32Wrapping(cubic.m_maxActivateProb, 1)) + 0.0
        );
        for (CubicSkillInfo* const skill : cubic.m_skillList) {
            if (skill != nullptr && roll <= skill->m_ActivateProb &&
                CheckCubicSkillDelay(creature, cubic, *skill->m_pSkillInfo, 0, activationInfo)) {
                return skill;
            }
        }
        return nullptr;
    }

    for (int index = 0; index < cubic.m_CubicTimeSkillCount; ++index) {
        CubicSkillInfo& skill = cubic.m_CubicTimeSkills[index];
        if ((skill.m_eTimeSkillType != CTST_DAILY || activationInfo.m_bResetDailySkills[index]) &&
            CheckCubicSkillDelay(creature, cubic, *skill.m_pSkillInfo, index, activationInfo)) {
            activateInfo.m_SkillIndex = index;
            return &skill;
        }
    }
    return nullptr;
}

bool CCubicHandler::ActivateCubicSkill(
    CCreature* creature, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo, CCubic* cubic
) {
    // Current L2Server 0x00638ac4.
    L2_GUARD_FUNCTION();

    CSkillInfo* const skillInfo = activateInfo.m_pSkillInfo;
    if (!CheckCubicSkillDelay(*creature, *cubic, *skillInfo, activateInfo.m_SkillIndex, activationInfo)) {
        return false;
    }

    CWorldObject* const target = activateInfo.m_TargetSID.GetWorldObject();
    if (activateInfo.m_NeedCheckTarget && !skillInfo->IsValidTarget(creature, target, false)) {
        return false;
    }

    if (!skillInfo->CubicActivate(creature, target, cubic->m_power)) {
        if (activationInfo.m_bTimeSkill) {
            CIOObject::SetTimeStamp(&activationInfo.m_TimeSkillLaunched[activateInfo.m_SkillIndex]);
        }
        creature->SetSkillReuseDelay(*skillInfo);
        return false;
    }

    if (!activationInfo.m_bTimeSkill) {
        ++activationInfo.hitCount;
        activationInfo.lastHitTime = runtime::crt::Time32Now();
        if (cubic->m_maxCount <= activationInfo.hitCount && cubic->m_UseUp) {
            creature->DespawnCubicBySlot(cubic->m_slot);
        }
    } else {
        if (cubic->m_CubicTimeSkills[activateInfo.m_SkillIndex].m_eTimeSkillType == CTST_DAILY) {
            activationInfo.m_bResetDailySkills[activateInfo.m_SkillIndex] = false;
        }
        CIOObject::SetTimeStamp(&activationInfo.m_TimeSkillLaunched[activateInfo.m_SkillIndex]);
    }

    creature->SetSkillReuseDelay(*skillInfo);
    return true;
}

bool CCubicHandler::ReadyToUseSkill(
    CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x0063b4bc.
    L2_GUARD_FUNCTION();

    if (!activationInfo.m_bTimeSkill) {
        const int delay = activationInfo.hitCount < cubic->m_maxCount ? cubic->m_delay
                                                                      : runtime::MultiplyInt32Wrapping(cubic->m_delay, 5);
        const std::int32_t now = runtime::crt::Time32Now();
        if (activationInfo.lastHitTime != 0 && now < runtime::AddInt32Wrapping(activationInfo.lastHitTime, delay)) {
            return false;
        }
    }

    switch (cubic->m_targetType) {
        case STT_TARGET:
            return ActionToCurrentTarget(creature, cubic, activationInfo, activateInfo);
        case STT_BY_SKILL:
            return ActionToTargetBySkill(creature, cubic, activationInfo, activateInfo);
        case STT_HEAL:
            return ActionHeal(creature, cubic, activationInfo, activateInfo, nullptr);
        case STT_MASTER:
            return ActionToMaster(creature, cubic, activationInfo, activateInfo);
        default:
            return false;
    }
}

bool CCubicHandler::ActionToCurrentTarget(
    CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x00638e24.
    L2_GUARD_FUNCTION();

    CubicSkillInfo* const skill = ChooseSkill(*creature, *cubic, activationInfo, activateInfo);
    if (skill == nullptr) {
        return false;
    }
    return TryToUseSkill(creature, creature->m_spTarget.GetWorldObject(), cubic, activationInfo, activateInfo, skill);
}

bool CCubicHandler::ActionToMaster(
    CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x00638f20.
    L2_GUARD_FUNCTION();

    CubicSkillInfo* const skill = ChooseSkill(*creature, *cubic, activationInfo, activateInfo);
    if (skill == nullptr) {
        return false;
    }
    return TryToUseSkill(creature, creature, cubic, activationInfo, activateInfo, skill);
}

bool CCubicHandler::ActionHeal(
    CCreature* creature,
    CCubic* cubic,
    CubicActivationInfo& activationInfo,
    ActivateInfo2& activateInfo,
    CubicSkillInfo* cubicSkillInfo
) {
    // Current L2Server 0x0063aec8.
    L2_GUARD_FUNCTION();

    struct HealCandidate {
        CCreature* creature;
        double distanceSquared;
        double currentHp;
        double maximumHp;
    };

    CCreature::SkillTargetData targetData;
    creature->GatherNeighborParty(targetData, -2, 0, 0, creature, false);

    std::vector<HealCandidate> candidates;
    candidates.reserve(targetData.c.size());
    for (std::size_t index = 0; index < targetData.c.size(); ++index) {
        CSPointer<CCreature> pointer = runtime::CheckedVectorAt(targetData.c, index).FindObjectSP();
        CCreature* const candidate = pointer.get();
        if (candidate == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%S][%d] SOMETHING WRONG!!!",
            //     "Cubic.cpp",
            //     0x2f8)
            // L2_LOG_ERROR(Log,
            //     "[%S][%d] SOMETHING WRONG!!!",
            //     "Cubic.cpp",
            //     0x2f8)
            L2_LOG_WARN(
                Log,
                "Cubic healing skips unavailable party member at candidate index %zu of %zu; remaining candidates continue.",
                index, targetData.c.size()
            );
            continue;
        }
        candidates.push_back(
            HealCandidate{
                candidate, runtime::CheckedVectorAt(targetData.d, index), candidate->m_data->m_dHP, candidate->m_data->m_dBaseHP}
        );
    }

    std::sort(candidates.begin(), candidates.end(), [](const HealCandidate& left, const HealCandidate& right) {
        return left.currentHp / left.maximumHp < right.currentHp / right.maximumHp;
    });

    CubicSkillInfo* selectedSkill = cubicSkillInfo;
    if (selectedSkill == nullptr) {
        double currentHpSum = 0.0;
        double maximumHpSum = 0.0;
        const std::size_t sampleCount = std::min<std::size_t>(candidates.size(), 2);
        for (std::size_t index = 0; index < sampleCount; ++index) {
            currentHpSum += candidates[index].currentHp;
            maximumHpSum += candidates[index].maximumHp;
        }
        const double partyHpRatio = currentHpSum / maximumHpSum;
        if (cubic->m_targetTypeOption[3] < partyHpRatio) {
            return false;
        }

        selectedSkill = cubic->m_skillList[2];
        for (int index = 2; index >= 0; --index) {
            if (partyHpRatio < cubic->m_targetTypeOption[index]) {
                selectedSkill = cubic->m_skillList[index];
                break;
            }
        }
        if (selectedSkill == nullptr) {
            return false;
        }
    }

    if (!(::genrand64_real1() * 100.0 + 0.0 < static_cast<double>(selectedSkill->m_HitProb))) {
        return false;
    }
    CSkillInfo* const skillInfo = selectedSkill->m_pSkillInfo;
    if (skillInfo == nullptr) {
        return false;
    }

    const double squaredCastRange =
        static_cast<double>(runtime::MultiplyInt32Wrapping(skillInfo->m_nCastRange, skillInfo->m_nCastRange));
    for (const HealCandidate& candidate : candidates) {
        if (candidate.currentHp == candidate.maximumHp) {
            continue;
        }
        if (squaredCastRange < candidate.distanceSquared) {
            continue;
        }
        if (!CheckOpCond(nullptr, candidate.currentHp, candidate.maximumHp, cubic->m_opCond) ||
            !CheckOpCond(nullptr, candidate.currentHp, candidate.maximumHp, selectedSkill->m_OpCond)) {
            continue;
        }
        if (creature->GetRemainReuseDelaySec(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel) != 0.0) {
            return false;
        }

        activateInfo.m_pSkillInfo = skillInfo;
        activateInfo.m_TargetSID = candidate.creature;
        activateInfo.m_NeedCheckTarget = false;
        activateInfo.m_CountDown = 3;
        (void)activationInfo;
        return true;
    }
    return false;
}

bool CCubicHandler::ActionToTargetBySkill(
    CCreature* creature, CCubic* cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x0063b374.
    L2_GUARD_FUNCTION();

    CubicSkillInfo* const skill = ChooseSkill(*creature, *cubic, activationInfo, activateInfo);
    if (skill == nullptr) {
        return false;
    }

    switch (skill->m_TargetType) {
        case STT_TARGET:
            return TryToUseSkill(
                creature, creature->m_spTarget.GetWorldObject(), cubic, activationInfo, activateInfo, skill
            );
        case STT_HEAL:
            return ActionHeal(creature, cubic, activationInfo, activateInfo, skill);
        case STT_MASTER:
            return TryToUseSkill(creature, creature, cubic, activationInfo, activateInfo, skill);
        default:
            return false;
    }
}

bool CCubicHandler::OnTick(
    CCreature& creature, CCubic& cubic, CubicActivationInfo& activationInfo, ActivateInfo2& activateInfo
) {
    // Current L2Server 0x0063b630.
    L2_GUARD_FUNCTION();

    CSkillInfo* const skillInfo = activateInfo.m_pSkillInfo;
    if (activateInfo.m_CountDown == -1) {
        return ReadyToUseSkill(&creature, &cubic, activationInfo, activateInfo);
    }

    if (activateInfo.m_CountDown == 0) {
        if (skillInfo != nullptr) {
            ActivateCubicSkill(&creature, activationInfo, activateInfo, &cubic);
        }
        activateInfo.m_CountDown = runtime::SubtractInt32Wrapping(activateInfo.m_CountDown, 1);
        return true;
    }

    if (activateInfo.m_CountDown == 3) {
        CWorldObject* const target = activateInfo.m_TargetSID.GetWorldObject();
        if (target == nullptr || skillInfo == nullptr) {
            activateInfo.m_CountDown = -1;
            return true;
        }
        creature.BroadcastCubicSkillUse(
            *target, skillInfo, runtime::Win64CvttDoubleToInt32(skillInfo->m_dSkillHitTime * 1000.0),
            runtime::Win64CvttDoubleToInt32(skillInfo->m_nReuseDelay)
        );
    }

    activateInfo.m_CountDown = runtime::SubtractInt32Wrapping(activateInfo.m_CountDown, 1);
    return true;
}

bool CCubicHandler::ResetLaunchedTime(CCubic& cubic, CubicActivationInfo& activationInfo, std::int32_t launchedTime) {
    L2_GUARD_FUNCTION();

    for (int index = 0; index < cubic.m_CubicTimeSkillCount; ++index) {
        if (cubic.m_CubicTimeSkills[index].m_eTimeSkillType == CTST_DAILY) {
            activationInfo.m_TimeSkillLaunched[index] = launchedTime;
            activationInfo.m_bResetDailySkills[index] = true;
        }
    }
    return true;
}

bool CCubicHandler::IsExpired(CCubic* cubic, CubicActivationInfo& activationInfo) {
    L2_GUARD_FUNCTION();

    if (cubic == nullptr) {
        return false;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    return activationInfo.summonedTime == 0 ||
        runtime::AddInt32Wrapping(activationInfo.summonedTime, cubic->m_duration) <= now;
}

bool CCubicHandler::CheckCubicSkillDelay(
    CCreature& creature, CCubic& cubic, CSkillInfo& skillInfo, int skillIndex, CubicActivationInfo& activationInfo
) {
    L2_GUARD_FUNCTION();

    if (activationInfo.m_bTimeSkill) {
        const int launchedTime = activationInfo.m_TimeSkillLaunched[skillIndex];
        const std::int32_t now = runtime::crt::Time32Now();
        if (launchedTime != 0 &&
            now < runtime::AddInt32Wrapping(cubic.m_CubicTimeSkills[skillIndex].m_CycleSec, launchedTime)) {
            return false;
        }
    }
    return creature.GetRemainReuseDelaySec(skillInfo.m_nSkillId, skillInfo.m_nLevel, skillInfo.m_nSubLevel) == 0.0;
}

void ActivateInfo2::Init() noexcept {
    m_NeedCheckTarget = true;
    m_pSkillInfo = nullptr;
    m_SkillIndex = 0;
    m_CountDown = -1;
}

CubicActivationInfo::CubicActivationInfo(std::int32_t summonedTimeValue, bool bySelfCastingValue) {
    Init(summonedTimeValue, bySelfCastingValue);
}

CubicActivationInfo::CubicActivationInfo(const CubicActivationInfo& other) {
    summonedTime = other.summonedTime;
    lastHitTime = other.lastHitTime;
    hitCount = other.hitCount;
    bySelfCasting = other.bySelfCasting;
    m_bTimeSkill = other.m_bTimeSkill;
    m_tmLastResetTime = other.m_tmLastResetTime;

    m_Normal.m_NeedCheckTarget = other.m_Normal.m_NeedCheckTarget;
    m_Normal.m_TargetSID = other.m_Normal.m_TargetSID;
    m_Normal.m_pSkillInfo = other.m_Normal.m_pSkillInfo;
    m_Normal.m_SkillIndex = other.m_Normal.m_SkillIndex;
    m_Normal.m_CountDown = other.m_Normal.m_CountDown;

    m_TimeSkill.m_NeedCheckTarget = other.m_TimeSkill.m_NeedCheckTarget;
    m_TimeSkill.m_TargetSID = other.m_TimeSkill.m_TargetSID;
    m_TimeSkill.m_pSkillInfo = other.m_TimeSkill.m_pSkillInfo;
    m_TimeSkill.m_SkillIndex = other.m_TimeSkill.m_SkillIndex;
    m_TimeSkill.m_CountDown = other.m_TimeSkill.m_CountDown;

    for (int index = 0; index < 5; ++index) {
        m_TimeSkillLaunched[index] = other.m_TimeSkillLaunched[index];
        m_bResetDailySkills[index] = other.m_bResetDailySkills[index];
    }
}

void CubicActivationInfo::Init(std::int32_t summonedTimeValue, bool bySelfCastingValue) {
    L2_GUARD_FUNCTION();

    summonedTime = summonedTimeValue;
    bySelfCasting = bySelfCastingValue;
    lastHitTime = 0;
    hitCount = 0;
    m_bTimeSkill = false;
    m_Normal.Init();
    m_TimeSkill.Init();

    for (int index = 0; index < 5; ++index) {
        m_TimeSkillLaunched[index] = summonedTime;
        m_bResetDailySkills[index] = true;
    }

    InitLastResetTime(summonedTime, 6, 30, false);
}

bool CubicActivationInfo::IsOnDailyTimeSkillResetTime(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();

    const std::int32_t nextReset = runtime::AddInt32Wrapping(m_tmLastResetTime, 86400);
    if (nextReset < currentTime) {
        m_tmLastResetTime = currentTime;
        return true;
    }
    return false;
}

void CubicActivationInfo::InitLastResetTime(std::int32_t currentTime, int resetHour, int resetMinute, bool usePreviousDay) {
    L2_GUARD_FUNCTION();

    std::tm currentLocal{};
    std::tm resetLocal{};
    runtime::crt::LocalTime32(currentTime, currentLocal);
    runtime::crt::LocalTime32(currentTime, resetLocal);
    resetLocal.tm_hour = resetHour & 0xffff;
    resetLocal.tm_min = resetMinute & 0xffff;

    std::int32_t resetTime = runtime::crt::Mktime32(&resetLocal);
    if (currentLocal.tm_hour < 6 || (currentLocal.tm_hour == 6 && currentLocal.tm_min < 29)) {
        resetTime = runtime::AddInt32Wrapping(resetTime, -86400);
    }
    if (usePreviousDay) {
        resetTime = runtime::AddInt32Wrapping(resetTime, -86400);
    }
    m_tmLastResetTime = resetTime;
}

#include "Transform.h"

#include "ParserTransform.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/TextRuntime.h"

#include "CWorld.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"
#include "WorldCollision.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/SharedFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

CTransformDB g_TransformDB;

namespace {

bool IsEnoughSpace(
    const FVector& position, const int& radius, const bool isUnderWater, const int waterSurfaceHeight, const int inZoneId
) {
    const double radiusValue = static_cast<double>(radius);
    const std::array<FVector, 4> offsets{{
        FVector{radiusValue, 0.0, 0.0},
        FVector{0.0, radiusValue, 0.0},
        FVector{-radiusValue, 0.0, 0.0},
        FVector{0.0, -radiusValue, 0.0},
    }};
    FVector collisionPosition{};

    for (const FVector& offset : offsets) {
        const FVector destination{
            position.x + offset.x,
            position.y + offset.y,
            position.z + offset.z,
        };
        const double distance = std::sqrt(
            (destination.x - position.x) * (destination.x - position.x) +
            (destination.y - position.y) * (destination.y - position.y) +
            (destination.z - position.z) * (destination.z - position.z)
        );

        if (!CWorld::Inst().GetGeo()->MoveStraightTest(position, destination, distance, isUnderWater, waterSurfaceHeight)) {
            return false;
        }
        if (g_WorldPlaneCollision.CheckCollision(position, destination, radiusValue, collisionPosition, inZoneId, true)) {
            return false;
        }
    }
    return true;
}

bool ValidatePosition(User* const user, const int& radius, const bool relocate) {
    L2_GUARD_FUNCTION();

    const double radiusValue = static_cast<double>(radius);
    const std::array<FVector, 8> offsets{{
        FVector{radiusValue, radiusValue, 0.0},
        FVector{0.0, radiusValue, 0.0},
        FVector{-radiusValue, radiusValue, 0.0},
        FVector{-radiusValue, 0.0, 0.0},
        FVector{-radiusValue, -radiusValue, 0.0},
        FVector{0.0, -radiusValue, 0.0},
        FVector{radiusValue, -radiusValue, 0.0},
        FVector{radiusValue, 0.0, 0.0},
    }};
    FVector& position = user->m_data->m_Pos;
    FVector lastValidPosition{};
    FVector collisionPosition{};
    bool allClear = true;
    bool foundValidPosition = false;

    for (const FVector& offset : offsets) {
        const FVector destination{
            position.x + offset.x,
            position.y + offset.y,
            position.z + offset.z,
        };
        const bool floating =
            static_cast<std::int32_t>(user->m_data->m_moveType) == CMT_FLY || user->m_AreaMod.m_bIsUnderWater;
        const double distance = std::sqrt(
            (destination.x - position.x) * (destination.x - position.x) +
            (destination.y - position.y) * (destination.y - position.y) +
            (destination.z - position.z) * (destination.z - position.z)
        );

        if (CWorld::Inst().GetGeo()->MoveStraightTest(
                position, destination, distance, floating, user->m_AreaMod.m_nWaterSurfaceHeight
            ) &&
            !g_WorldPlaneCollision.CheckCollision(
                position, destination, radiusValue, collisionPosition, user->GetInZoneID(), true
            )) {
            if (!IsEnoughSpace(
                    destination, radius, user->m_AreaMod.m_bIsUnderWater, user->m_AreaMod.m_nWaterSurfaceHeight,
                    user->GetInZoneID()
                )) {
                allClear = false;
            } else {
                foundValidPosition = true;
                lastValidPosition = destination;
            }
        }
    }

    if (foundValidPosition && !allClear && relocate) {
        position = lastValidPosition;
    }
    return foundValidPosition;
}

} // namespace

CTransform::CTransform(const int transformId) :
    m_TransformId(transformId),
    m_swimable(false),
    m_commonData(nullptr),
    m_buf{},
    m_bufLen(0),
    m_actionCount(0),
    m_eTransformType(TransformType::NotTransform),
    m_SpawnHeight(0),
    m_bNormalAttakable(true),
    m_bCanMove(true) {}

void CTransformCombatData::Init() {
    L2_GUARD_FUNCTION();

    m_Stat.Init();
    std::memset(basePDefend, 0, sizeof(basePDefend));
    std::memset(baseMDefend, 0, sizeof(baseMDefend));
    std::memset(orgHPRegen, 0, sizeof(orgHPRegen));
    std::memset(orgMPRegen, 0, sizeof(orgMPRegen));
    std::memset(orgCPRegen, 0, sizeof(orgCPRegen));
    std::memset(levelBonus, 0, sizeof(levelBonus));
    std::memset(strBonus, 0, sizeof(strBonus));
    std::memset(intBonus, 0, sizeof(intBonus));
    std::memset(conBonus, 0, sizeof(conBonus));
    std::memset(dexBonus, 0, sizeof(dexBonus));
    std::memset(witBonus, 0, sizeof(witBonus));
    std::memset(menBonus, 0, sizeof(menBonus));
    std::memset(hpTable, 0, sizeof(hpTable));
    std::memset(mpTable, 0, sizeof(mpTable));
    std::memset(cpTable, 0, sizeof(cpTable));
    std::memset(lucBonus, 0, sizeof(lucBonus));
    std::memset(chaBonus, 0, sizeof(chaBonus));
}

void CTransformCombatData::SetStat(std::vector<int>& stats) {
    L2_GUARD_FUNCTION();

    if (stats.size() != 8) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CTransformCombatData SetStat data wrong [%d]",
        //     static_cast<int>(stats.size()))
        // L2_LOG_ERROR(Log,
        //     L"CTransformCombatData SetStat data wrong [%d]",
        //     static_cast<int>(stats.size()))
        L2_LOG_ERROR(
            Log, "Cannot assign transformation base stats: received %zu values, expected 8; previous stats are retained.",
            static_cast<std::size_t>(stats.size())
        );
        return;
    }

    m_Stat.m_N[0] = runtime::CheckedVectorAt(stats, 1);
    m_Stat.m_N[1] = runtime::CheckedVectorAt(stats, 0);
    m_Stat.m_N[2] = runtime::CheckedVectorAt(stats, 2);
    m_Stat.m_N[3] = runtime::CheckedVectorAt(stats, 5);
    m_Stat.m_N[4] = runtime::CheckedVectorAt(stats, 3);
    m_Stat.m_N[5] = runtime::CheckedVectorAt(stats, 4);
    m_Stat.m_N[6] = runtime::CheckedVectorAt(stats, 6);
    m_Stat.m_N[7] = runtime::CheckedVectorAt(stats, 7);
}

void CTransformCombatData::SetStatByLevel(const TransformLevelStat stat, std::vector<double>& values) {
    L2_GUARD_FUNCTION();

    double* destination = orgHPRegen;
    switch (stat) {
        case TR_HP_REGEN:
            break;
        case TR_MP_REGEN:
            destination = orgMPRegen;
            break;
        case TR_CP_REGEN:
            destination = orgCPRegen;
            break;
        case TR_LEVEL_BONUS:
            destination = levelBonus;
            break;
        case TR_STR_BONUS:
            destination = strBonus;
            break;
        case TR_INT_BONUS:
            destination = intBonus;
            break;
        case TR_CON_BONUS:
            destination = conBonus;
            break;
        case TR_DEX_BONUS:
            destination = dexBonus;
            break;
        case TR_WIT_BONUS:
            destination = witBonus;
            break;
        case TR_MEN_BONUS:
            destination = menBonus;
            break;
        case TR_HP_TABLE:
            destination = hpTable;
            break;
        case TR_MP_TABLE:
            destination = mpTable;
            break;
        case TR_CP_TABLE:
            destination = cpTable;
            break;
        case TR_LUC_BONUS:
            destination = lucBonus;
            break;
        case TR_CHA_BONUS:
            destination = chaBonus;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid parameters on Loading Transform Data "
            //     L"[%d]",
            //     L"Transform.cpp",
            //     0x1a8,
            //     static_cast<int>(stat))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid parameters on Loading Transform Data "
            //     L"[%d]",
            //     L"Transform.cpp",
            //     0x1a8,
            //     static_cast<int>(stat))
            L2_LOG_ERROR(
                Log,
                "Unknown transformation level-stat field %d; the supplied values are still written to the HP regeneration "
                "table.",
                static_cast<int>(stat)
            );
            break;
    }

    const bool normalizedBonus = static_cast<unsigned int>(stat - TR_STR_BONUS) < 6U;
    for (const double value : values) {
        *destination++ = normalizedBonus ? (value + 1000.0) / 1000.0 : value;
    }
}

void CTransformCombatData::SetPDefend(std::vector<double>& values) {
    L2_GUARD_FUNCTION();

    basePDefend[7] = 0.0;
    double* destination = basePDefend;
    for (const double value : values) {
        *destination++ = value;
        basePDefend[7] += value;
    }
}

void CTransformCombatData::SetMDefend(std::vector<double>& values) {
    L2_GUARD_FUNCTION();

    baseMDefend[13] = 0.0;
    double* destination = baseMDefend + 8;
    for (const double value : values) {
        *destination++ = value;
        baseMDefend[13] += value;
    }
}

CTransformCommonData::CTransformCommonData() { Init(); }

CTransformDB::CTransformDB() : m_pLastInsertedMaleTransform(nullptr), m_pLastInsertedFemaleTransform(nullptr) {}

bool CTransformDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kTransformPath = L"..\\Script\\transform.txt";
#else
    constexpr const wchar_t* kTransformPath = L"../script/transform.txt";
#endif

    CSecureStream input(kTransformPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Transform file failed",
        //     L"Transform.cpp",
        //     0x3b4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Transform file failed",
        //     L"Transform.cpp",
        //     0x3b4)
        L2_LOG_ERROR(Log, "Cannot read transform.txt; transformation loading fails.");
        return false;
    }

    if (!Load(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        return false;
    }

    PostLoad();
    return true;
}

bool CTransformDB::Load(char16_t* const fileBuffer, const int fileSize) {
    L2_GUARD_FUNCTION();

    CParserForTransform parser;
    if (!parser.Create(fileBuffer, fileSize)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TransformParse.create failed",
        //     L"Transform.cpp",
        //     0x3ce)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] TransformParse.create failed",
        //     L"Transform.cpp",
        //     0x3ce)
        L2_LOG_ERROR(Log, "Cannot initialize the transformation-data parser; loading fails.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Transform file failed after line[%d]",
        //     L"Transform.cpp",
        //     0x3db,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Transform file failed after line[%d]",
        //     L"Transform.cpp",
        //     0x3db,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse transformation data at script line %lld; loading fails and already inserted definitions are "
            "retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
        return false;
    }
    return true;
}

bool CTransform::CanTransform(User* const user) {
    L2_GUARD_FUNCTION();

    const double collisionRadius = m_commonData->collisionRadius;
    if (user->m_data->m_dCollisionRadius <= collisionRadius && collisionRadius != user->m_data->m_dCollisionRadius) {
        const int radius = static_cast<int>(collisionRadius);
        if (!ValidatePosition(user, radius, false)) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_TRANSFORM_NOT_ENOUGH_SPACE);
            return false;
        }
    }

    if (!m_swimable && user->m_AreaMod.m_bIsUnderWater) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_DESIRED_TRANSFORMATION_UNDERWATER);
        return false;
    }
    return true;
}

bool CTransform::SetCollision(User* const user, const int radius, const int height, const bool restore) {
    L2_GUARD_FUNCTION();

    CSharedCreatureData* const data = user->m_data;
    if (restore) {
        data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dCollisionRadius, static_cast<double>(radius), 0x1b);
        data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dCollisionHeight, static_cast<double>(height), 0x1c);
    }

    if (!user->IsInVehicle() && !user->IsInAirShip() && !user->IsInShuttle() &&
        data->m_dCollisionRadius <= static_cast<double>(radius) && static_cast<double>(radius) != data->m_dCollisionRadius &&
        data->container_index != -1 && !ValidatePosition(user, radius, true)) {
        const FVector position = user->GetPos();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] transform failed (invalid pos). id[%d], "
        //     L"gender[%d], name[%s] pos[%d %d %d]",
        //     L"Transform.cpp",
        //     0x2b0,
        //     m_TransformId,
        //     static_cast<int>(data->m_Sex),
        //     user->GetName(),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] transform failed (invalid pos). id[%d], "
        //     L"gender[%d], name[%s] pos[%d %d %d]",
        //     L"Transform.cpp",
        //     0x2b0,
        //     m_TransformId,
        //     static_cast<int>(data->m_Sex),
        //     user->GetName(),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     "Cannot set collision for transformation %d, player '%s', sex %d at (%.17g, %.17g, %.17g): position validation
        //     rejected radius %d, height %d (restore %d).", m_TransformId, WideToUtf8(user->GetName()).c_str(),
        //     static_cast<int>(data->m_Sex),
        //     position.x,
        //     position.y,
        //     position.z,
        //     radius,
        //     height,
        //     static_cast<int>(restore))
        L2_LOG_ERROR(
            Log,
            "Cannot set collision for transformation %d, player '%s', sex %d at (%.17g, %.17g, %.17g): position validation "
            "rejected radius %d, height %d (restore %d).",
            m_TransformId, Utf16ToUtf8(user->GetName()).c_str(), static_cast<int>(data->m_Sex), position.x, position.y,
            position.z, radius, height, static_cast<int>(restore)
        );
        runtime::process::ReportCallStack(2, false);
        return false;
    }

    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dCollisionRadius, static_cast<double>(radius), 0x1b);
    data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(data->m_dCollisionHeight, static_cast<double>(height), 0x1c);
    return true;
}

void CTransformDB::PostLoad() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_transformMap) {
        entry.second->PostLoad();
    }
}

void CTransformDB::Add(const int transformId, const SexType sex, CTransform* const transform) {
    L2_GUARD_FUNCTION();

    m_transformMap[{transformId, sex}] = transform;
    if (sex == ST_MALE) {
        m_pLastInsertedMaleTransform = transform;
    } else if (sex == ST_FEMALE) {
        m_pLastInsertedFemaleTransform = transform;
    }
}

CTransform* CTransformDB::Find(const int transformId, const SexType sex) {
    L2_GUARD_FUNCTION();

    return m_transformMap[{transformId, sex}];
}

void CTransform::SendActionList(User* const user) {
    L2_GUARD_FUNCTION();

    user->GetSocket()->Send("chdb", 0xfe, 0x60, m_actionCount, m_bufLen, m_buf.data());
}

bool CTransform::_SetCommonData(User* const user) {
    L2_GUARD_FUNCTION();

    if (!SetCollision(
            user, static_cast<int>(m_commonData->collisionRadius), static_cast<int>(m_commonData->collisionHeight), false
        )) {
        return false;
    }

    CSharedCreatureData* const data = user->m_data;
    data->m_BaseAttackType = m_commonData->baseAttackType;
    data->m_nBaseAttackRange = m_commonData->bareHandsData.attackRange;
    data->m_nBaseRandomDamage = m_commonData->bareHandsData.randomDamage;
    data->m_nBaseParam[2] = m_commonData->bareHandsData.pAttack;
    data->m_nBaseParam[3] = m_commonData->bareHandsData.mAttack;
    data->m_nBaseParam[4] = m_commonData->bareHandsData.attackSpeed;
    data->m_nBaseParam[9] = m_commonData->bareHandsData.criticalProb;
    return true;
}

void CTransformForCombat::_SetCombatData(User* const user) {
    L2_GUARD_FUNCTION();

    CSharedCreatureData* const data = user->m_data;
    data->m_OriginalStat = m_combatData->m_Stat;
    data->SetStats(data->m_OriginalStat);
    data->m_nBaseParam[0] = static_cast<int>(m_combatData->basePDefend[7]);
    data->m_nBaseParam[1] = static_cast<int>(m_combatData->baseMDefend[13]);
}

bool CTransformForFlying::_SetCommonData(User* const user) {
    L2_GUARD_FUNCTION();

    const bool result = CTransform::_SetCommonData(user);
    if (result) {
        CSharedCreatureData* const data = user->m_data;
        data->m_moveType = CMT_FLY;
        data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
            reinterpret_cast<std::int32_t&>(data->m_Environment), ENV_AIR, 0x53
        );
    }
    return result;
}

void CTransformForFlying::OnCancelTransform(User* const user) {
    L2_GUARD_FUNCTION();

    CSharedCreatureData* const data = user->m_data;
    data->m_moveType = CMT_WALK;
    data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(data->m_Environment), ENV_GROUND, 0x53
    );
    user->ReflectWaterEffect();
}

bool CTransform::SetTransformData(User* const user) {
    L2_GUARD_FUNCTION();

    const bool result = _SetCommonData(user);
    if (result) {
        _SetCombatData(user);
    }
    return result;
}

void CTransform::ConvertTransformTypes(const std::vector<wchar_t*>& transformTypes, std::vector<int>& convertedTypes) {
    L2_GUARD_FUNCTION();

    for (const wchar_t* const transformType : transformTypes) {
        const int convertedType = GetTransformType(transformType);
        if (convertedType == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] transform_type is invalid",
            //     transformType)
            // L2_LOG_ERROR(Log,
            //     L"[%s] transform_type is invalid",
            //     transformType)
            L2_LOG_ERROR(Log, "Unknown transformation type '%s'; this type is skipped.", WideToUtf8(transformType).c_str());
        } else {
            convertedTypes.push_back(convertedType);
        }
    }
}

int CTransform::GetTransformType(const wchar_t* const transformType) {
    L2_GUARD_FUNCTION();

    struct TransformTypeName {
        const wchar_t* name;
        int value;
    };
    const TransformTypeName transformTypes[] = {
        {L"tt_non_transform", TransformType::NotTransform},
        {L"tt_non_combat", TransformType::NonCombat},
        {L"tt_combat", TransformType::Combat},
        {L"tt_cursed", TransformType::Cursed},
        {L"tt_mode_change", TransformType::ModeChange},
        {L"tt_riding_mode", TransformType::Riding},
        {L"tt_flying", TransformType::Flying},
        {L"tt_pure_stat", TransformType::PureStat},
    };

    for (const TransformTypeName& entry : transformTypes) {
        if (std::wcscmp(entry.name, transformType) == 0) {
            return entry.value;
        }
    }
    return -1;
}

void CTransform::GetPacketForUseItem(IPacketHelper& packet) const {
    L2_GUARD_FUNCTION();

    m_commonData->GetPacketForUseItem(packet);
}

bool CTransform::IsValidAction(const int action) {
    L2_GUARD_FUNCTION();

    return std::find(m_commonData->actionList.begin(), m_commonData->actionList.end(), action) !=
        m_commonData->actionList.end();
}

void CTransform::PostLoad() {
    L2_GUARD_FUNCTION();

    if (m_commonData == nullptr) {
        return;
    }

    m_bufLen = 0;
    m_actionCount = 0;
    for (auto position = m_commonData->actionList.begin(); position != m_commonData->actionList.end(); ++position) {
        ++m_actionCount;
        const int assembled = ::Assemble(m_buf.data() + m_bufLen, static_cast<int>(m_buf.size()) - m_bufLen, "d", *position);
        if (assembled > 0) {
            m_bufLen += assembled;
        }
    }
}

bool CTransform::CanUseItem(const int itemId) const {
    L2_GUARD_FUNCTION();

    return m_commonData->CanUseItem(itemId);
}

bool CTransformDB::HaveSameSkillBySkillID(
    const std::vector<const CSkillInfo*>* const skills, const CSkillInfo* const skill
) {
    L2_GUARD_FUNCTION();

    if (skills == nullptr) {
        return false;
    }
    for (const CSkillInfo* const current : *skills) {
        if (current->m_nSkillId == skill->m_nSkillId) {
            return true;
        }
    }
    return false;
}

const std::vector<const CSkillInfo*>* CTransform::AdditionalSkillList(const int level) {
    L2_GUARD_FUNCTION();

    int selectedLevel = 0;
    for (const auto& entry : m_commonData->additionalSkillMap) {
        if (entry.first <= level && selectedLevel < entry.first) {
            selectedLevel = entry.first;
        }
    }
    if (selectedLevel != 0) {
        return m_commonData->additionalSkillMap[selectedLevel];
    }
    return nullptr;
}

void CTransform::GetSkillList(const int level, std::vector<const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();

    const std::vector<const CSkillInfo*>* const additionalSkills = AdditionalSkillList(level);
    for (const CSkillInfo* const skill : m_commonData->skillList) {
        if (skill != nullptr && !CTransformDB::HaveSameSkillBySkillID(additionalSkills, skill)) {
            skills.push_back(skill);
        }
    }
    if (additionalSkills != nullptr) {
        for (const CSkillInfo* const skill : *additionalSkills) {
            skills.push_back(skill);
        }
    }
}

int CTransform::GetSkillLevel(const int level, const int skillId) {
    L2_GUARD_FUNCTION();

    std::vector<const CSkillInfo*> skills;
    GetSkillList(level, skills);
    for (const CSkillInfo* const skill : skills) {
        if (skill->m_nSkillId == skillId) {
            return skill->m_nLevel;
        }
    }
    return -1;
}

void CTransformCommonData::SetSpeed(std::vector<double>& speed) {
    L2_GUARD_FUNCTION();

    const double* const values = speed.data();
    orgSpeed[1][3] = values[7];
    orgSpeed[0][3] = values[6];
    orgSpeed[1][2] = values[5];
    orgSpeed[0][2] = values[4];
    orgSpeed[1][1] = values[3];
    orgSpeed[0][1] = values[2];
    orgSpeed[1][0] = values[1];
    orgSpeed[0][0] = values[0];
}

void CTransformCommonData::GetPacketForUseItem(IPacketHelper& packet) const {
    L2_GUARD_FUNCTION();

    const std::int16_t itemCount = static_cast<std::int16_t>(m_ItemCheck.size());
    if (itemCount == 0) {
        packet.Assemble("h", 0);
        return;
    }

    packet.Assemble("hc", static_cast<int>(itemCount), m_bItemAllow);
    for (const int itemId : m_ItemCheck) {
        packet.Assemble("d", itemId);
    }
}

bool CTransformCommonData::CanUseItem(const int itemId) const {
    L2_GUARD_FUNCTION();

    if (m_ItemCheck.empty()) {
        return true;
    }
    const bool found = m_ItemCheck.find(itemId) != m_ItemCheck.end();
    return m_bItemAllow ? found : !found;
}

void CTransformCommonData::Init() {
    L2_GUARD_FUNCTION();

    collisionHeight = 0.0;
    collisionRadius = 0.0;
    std::memset(orgSpeed, 0, sizeof(orgSpeed));
    skillList.clear();
    actionList.clear();
    baseAttackType = AT_NONE;
    bareHandsData.attackRange = 0;
    bareHandsData.randomDamage = 0;
    bareHandsData.attackSpeed = 0;
    bareHandsData.criticalProb = 0;
    bareHandsData.pAttack = 0;
    bareHandsData.mAttack = 0;
}

void CTransformCommonData::SetItemCheck(const bool allow, const std::vector<int>& items) {
    L2_GUARD_FUNCTION();

    m_ItemCheck.clear();
    for (const int itemId : items) {
        m_ItemCheck.insert(itemId);
    }
    m_bItemAllow = allow;
}

void CTransformCommonData::SetAction(std::vector<int>& actions) {
    L2_GUARD_FUNCTION();

    actionList.assign(actions.begin(), actions.end());
}

void CTransformCommonData::SetSkill(std::vector<const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();

    for (const CSkillInfo* const skill : skills) {
        skillList.push_back(skill);
    }
}

void CTransformDB::UpdateSkillIfLevelHigher(std::vector<const CSkillInfo*>& skills, const CSkillInfo* const skill) {
    L2_GUARD_FUNCTION();

    for (const CSkillInfo*& current : skills) {
        if (current->m_nSkillId != skill->m_nSkillId) {
            continue;
        }
        if (current->m_nLevel < skill->m_nLevel) {
            current = skill;
        }
        return;
    }
    skills.push_back(skill);
}

void CTransformCommonData::SetAdditionalSkill(const int level, std::vector<const CSkillInfo*>& skills) {
    L2_GUARD_FUNCTION();

    auto* const merged = new std::vector<const CSkillInfo*>;
    for (const CSkillInfo* const skill : skills) {
        merged->push_back(skill);
    }

    for (auto& entry : additionalSkillMap) {
        if (level < entry.first) {
            for (const CSkillInfo* const skill : skills) {
                CTransformDB::UpdateSkillIfLevelHigher(*entry.second, skill);
            }
        } else if (entry.first < level) {
            for (const CSkillInfo* const skill : *entry.second) {
                CTransformDB::UpdateSkillIfLevelHigher(*merged, skill);
            }
        }
    }
    additionalSkillMap[level] = merged;
}

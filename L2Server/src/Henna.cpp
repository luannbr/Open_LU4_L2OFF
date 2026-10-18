#include "Henna.h"

#include "CWorld.h"
#include "Creature.h"
#include "HennaParser.h"
#include "Item.h"
#include "Object.h"
#include "ObjectDB.h"
#include "User.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

CHennaDB g_HennaDB;

CHennaCondition::~CHennaCondition() = default;

bool CHennaCondition::CheckCondition(CCreature*) { return true; }

bool CHennaCondition_is_in_battle_field::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x24)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x24)
        L2_LOG_ERROR(Log, "Cannot check henna battlefield condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x2a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x2a)
        L2_LOG_ERROR(
            Log, "Cannot check henna battlefield condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x32)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x32)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna battlefield condition for object %u: IsPC succeeded but CastUser returned null; condition "
            "fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    return user->IsInRealWorld() && !user->m_AreaMod.m_bIsInPvPZone && user->m_AreaMod.m_bIsInBattleField &&
        CWorld::Inst().GetGeo()->IsBattleField(user->GetPos());
}

bool CHennaCondition_is_in_castle_war::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x4c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x4c)
        L2_LOG_ERROR(Log, "Cannot check henna castle war condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x52)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x52)
        L2_LOG_ERROR(
            Log, "Cannot check henna castle war condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x5a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x5a)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna castle war condition for object %u: IsPC succeeded but CastUser returned null; condition "
            "fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    return user->IsInRealWorld() && !user->m_AreaMod.m_bIsInPvPZone && user->m_AreaMod.m_bIsInBattleField &&
        CWorld::Inst().GetGeo()->IsBattleField(user->GetPos()) && user->IsNoSiegeRelation();
}

bool CHennaCondition_hc_is_in_agit_war::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x75)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x75)
        L2_LOG_ERROR(Log, "Cannot check henna agit war condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x7b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0x7b)
        L2_LOG_ERROR(
            Log, "Cannot check henna agit war condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x83)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0x83)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna agit war condition for object %u: IsPC succeeded but CastUser returned null; condition "
            "fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    return user->IsInRealWorld() && !user->m_AreaMod.m_bIsInPvPZone && user->m_AreaMod.m_bIsInBattleField &&
        CWorld::Inst().GetGeo()->IsBattleField(user->GetPos()) && user->IsInAgitWar(false);
}

bool CHennaCondition_is_in_fortress_siege::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x9e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0x9e)
        L2_LOG_ERROR(Log, "Cannot check henna fortress siege condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xa4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xa4)
        L2_LOG_ERROR(
            Log, "Cannot check henna fortress siege condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xac)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xac)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna fortress siege condition for object %u: IsPC succeeded but CastUser returned null; "
            "condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    return user->IsInRealWorld() && !user->m_AreaMod.m_bIsInPvPZone && user->m_AreaMod.m_bIsInBattleField &&
        CWorld::Inst().GetGeo()->IsBattleField(user->GetPos()) && user->IsInFortressSiege();
}

bool CHennaCondition_is_in_olympiad::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0xc7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0xc7)
        L2_LOG_ERROR(Log, "Cannot check henna Olympiad condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xcd)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xcd)
        L2_LOG_ERROR(
            Log, "Cannot check henna Olympiad condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xd5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xd5)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna Olympiad condition for object %u: IsPC succeeded but CastUser returned null; condition "
            "fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }
    return user->IsOlympiadPlaying();
}

bool CHennaCondition_is_in_pledge_game::CheckCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0xea)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is nullptr",
        //     L"henna.cpp",
        //     0xea)
        L2_LOG_ERROR(Log, "Cannot check henna pledge game condition: creature is missing; condition fails.");
        return false;
    }
    if (!creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xf0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pCreature] is not PC",
        //     L"henna.cpp",
        //     0xf0)
        L2_LOG_ERROR(
            Log, "Cannot check henna pledge game condition for object %u: creature is not a player; condition fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xf8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] is not PC.",
        //     L"henna.cpp",
        //     0xf8)
        L2_LOG_ERROR(
            Log,
            "Cannot check henna pledge game condition for object %u: IsPC succeeded but CastUser returned null; condition "
            "fails.",
            static_cast<unsigned>(creature->m_nId)
        );
        return false;
    }
    return user->m_PledgeGameUser.IsPlayer();
}

CHenna::CHenna() :
    m_nClassId(0),
    m_nItemClassId(0),
    m_nHennaLevel(0),
    m_NeedCount(0),
    m_WearFee(0),
    m_CancelCount(0),
    m_CancelFee(0),
    m_bIsPremiumHenna(false),
    m_alignment79To7c{},
    m_nPeriod(0) {
    L2_GUARD_FUNCTION();
}

CHenna::~CHenna() { L2_GUARD_FUNCTION(); }

bool CHenna::CanWearClass(int classId) {
    L2_GUARD_FUNCTION();
    for (const std::int32_t allowedClass : m_vCanWearClass) {
        if (allowedClass == classId) {
            return true;
        }
    }
    return false;
}

bool CHenna::HasItem(CItem* item) {
    L2_GUARD_FUNCTION();
    return item == nullptr || item->m_data->m_nCount >= m_NeedCount;
}

bool CHenna::CanUse(int classId, CItem* item) {
    L2_GUARD_FUNCTION();
    return CanWearClass(classId) && HasItem(item);
}

bool CHenna::CheckHennaCondition(CCreature* creature) {
    L2_GUARD_FUNCTION();
    for (CHennaCondition* const condition : m_vHennaCondition) {
        if (!condition->CheckCondition(creature)) {
            return false;
        }
    }
    return true;
}

void CHenna::Set(ObjectFieldType field, int value) {
    L2_GUARD_FUNCTION();
    switch (field) {
        case HENNA_INT_ID:
            m_nClassId = value;
            break;
        case HENNA_ITEM_ID: {
            m_nItemClassId = value;
            if (m_nClassId == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"ERROR. Cannot set henna id. item[%d]",
                //     value)
                // L2_LOG_ERROR(Log,
                //     L"ERROR. Cannot set henna id. item[%d]",
                //     value)
                L2_LOG_ERROR(
                    Log, "Cannot assign henna ID to item %d: henna class ID is still 0; item-class assignment retained.",
                    value
                );
                break;
            }
            CObject* const object = g_ObjectDB.GetObjectW(value, false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr) {
                item->Set(HENNA_ITEM_ID, m_nClassId);
            }
            break;
        }
        case HENNA_LEVEL:
            m_nHennaLevel = value;
            break;
        case HENNA_STR:
            m_HennaBonus.m_N[1] = value;
            break;
        case HENNA_CON:
            m_HennaBonus.m_N[2] = value;
            break;
        case HENNA_DEX:
            m_HennaBonus.m_N[4] = value;
            break;
        case HENNA_INT:
            m_HennaBonus.m_N[0] = value;
            break;
        case HENNA_MEN:
            m_HennaBonus.m_N[3] = value;
            break;
        case HENNA_WIT:
            m_HennaBonus.m_N[5] = value;
            break;
        case HENNA_LUC:
            m_HennaBonus.m_N[6] = value;
            break;
        case HENNA_CHA:
            m_HennaBonus.m_N[7] = value;
            break;
        case HENNA_NEED_COUNT:
            m_NeedCount = value;
            break;
        case HENNA_WERA_FEE:
            m_WearFee = value;
            break;
        case HENNA_CANCEL_COUNT:
            m_CancelCount = value;
            break;
        case HENNA_CANCEL_FEE:
            m_CancelFee = value;
            break;
        case HENNA_PREMIUM:
            m_bIsPremiumHenna = value > 0;
            break;
        case HENNA_PERIOD:
            m_nPeriod = value;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]error.",
            //     "henna.cpp",
            //     0x19b)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]error.",
            //     "henna.cpp",
            //     0x19b)
            L2_LOG_ERROR(
                Log, "Henna %d: unsupported integer field %d with value %d; value ignored.", m_nClassId,
                static_cast<int>(field), value
            );
            break;
    }
}

void CHenna::Set(ObjectFieldType field, std::vector<int>* values) {
    L2_GUARD_FUNCTION();
    if (field != HENNA_CAN_WEAR_CLASS || values == nullptr) {
        return;
    }
    for (const int value : *values) {
        m_vCanWearClass.push_back(value);
    }
}

void CHenna::Set(ObjectFieldType field, const wchar_t* value) {
    L2_GUARD_FUNCTION();
    int skillId = 0;
    int level = 0;
    int subLevel = 0;

    std::vector<CSkillKey>* destination = nullptr;
    switch (field) {
        case HENNA_SKILL:
            destination = &m_HennaSkillList;
            break;
        case HENNA_UNEQUIP_SKILL:
            destination = &m_HennaUnequipSkillList;
            break;
        case HENNA_COND:
            if (g_SkillDB.GetSkillIdLevelByName(value, &skillId, &level, &subLevel)) {
                return;
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"Undefined skill name [%s]",
            //     value)
            // L2_LOG_ERROR(Log,
            //     L"Undefined skill name [%s]",
            //     value)
            L2_LOG_ERROR(
                Log, "Henna %d: condition skill '%s' is unknown.", m_nClassId, value ? WideToUtf8(value).c_str() : "<null>"
            );
            return;
        default:
            return;
    }

    if (g_SkillDB.GetSkillIdLevelByName(value, &skillId, &level, &subLevel)) {
        destination->push_back(CSkillKey(skillId, level, subLevel));
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"Undefined skill name [%s]",
    //     value)
    // L2_LOG_ERROR(Log,
    //     L"Undefined skill name [%s]",
    //     value)
    L2_LOG_ERROR(
        Log, "Henna %d: unknown %s '%s'; skill entry skipped.", m_nClassId, field == HENNA_SKILL ? "skill" : "unequip skill",
        value ? WideToUtf8(value).c_str() : "<null>"
    );
}

CHennaDB::CHennaDB() {}

CHennaDB::~CHennaDB() = default;

void CHennaDB::Add(CHenna* henna) {
    m_mapHennaInfo.emplace(henna->m_nClassId, henna);
    m_mapHennaInfoByItem.emplace(henna->m_nItemClassId, henna);
}

CHenna* CHennaDB::Find(int classId) {
    L2_GUARD_FUNCTION();
    const auto position = m_mapHennaInfo.find(classId);
    return position == m_mapHennaInfo.end() ? nullptr : position->second;
}

void CHennaDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDyeDataPath = L"..\\Script\\Dyedata.txt";
#else
    constexpr const wchar_t* kDyeDataPath = L"../script/Dyedata.txt";
#endif

    CSecureStream input(kDyeDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Dye file failed",
        //     L"henna.cpp",
        //     0x275)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Dye file failed",
        //     L"henna.cpp",
        //     0x275)
        L2_LOG_ERROR(Log, "Cannot read Dyedata.txt; henna loading stops.");
        return;
    }

    CParserForHenna parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"henna.cpp",
        //     0x27e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"henna.cpp",
        //     0x27e)
        L2_LOG_ERROR(Log, "Cannot initialize Dyedata.txt parser; henna loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Dye file failed after line[%d]",
        //     L"henna.cpp",
        //     0x28a,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Dye file failed after line[%d]",
        //     L"henna.cpp",
        //     0x28a,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse Dyedata.txt at script line %lld; henna entries registered before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

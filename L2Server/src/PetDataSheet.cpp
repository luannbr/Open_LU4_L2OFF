#include "PetDataSheet.h"
#include "Shared/src/TextRuntime.h"

#include "PetDataLexer.h"
#include "PetDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

namespace {


bool EqualsAsciiCaseInsensitive(const char16_t* left, const char16_t* right) {
    for (;;) {
        char16_t foldedLeft = *left++;
        char16_t foldedRight = *right++;
        if (foldedLeft >= u'A' && foldedLeft <= u'Z') {
            foldedLeft = static_cast<char16_t>(foldedLeft + (u'a' - u'A'));
        }
        if (foldedRight >= u'A' && foldedRight <= u'Z') {
            foldedRight = static_cast<char16_t>(foldedRight + (u'a' - u'A'));
        }
        if (foldedLeft != foldedRight) {
            return false;
        }
        if (foldedLeft == u'\0') {
            return true;
        }
    }
}

} // namespace

AttrTypeInfo PetDataSheet::msTable[27] = {
    {u"level", Record::LEVEL},
    {u"max_meal", Record::MAX_MEAL},
    {u"exp", Record::EXP},
    {u"consume_meal_in_battle", Record::CON_MEAL_BATTLE},
    {u"consume_meal_in_normal", Record::CON_MEAL_NORMAL},
    {u"org_pattack", Record::ORG_PATTACK},
    {u"org_pdefend", Record::ORG_PDEFEND},
    {u"org_mattack", Record::ORG_MATTACK},
    {u"org_mdefend", Record::ORG_MDEFEND},
    {u"org_hp", Record::ORG_HP},
    {u"org_mp", Record::ORG_MP},
    {u"org_hp_regen", Record::ORG_HPREGEN},
    {u"org_mp_regen", Record::ORG_MPREGEN},
    {u"speed_on_ride", Record::SPEED_ON_RIDE},
    {u"attack_speed_on_ride", Record::ATTACK_SPEED_ON_RIDE},
    {u"pattack_on_ride", Record::PATTACK_ON_RIDE},
    {u"mattack_on_ride", Record::MATTACK_ON_RIDE},
    {u"max_hp_on_ride", Record::MAX_HP_ON_RIDE},
    {u"max_mp_on_ride", Record::MAX_MP_ON_RIDE},
    {u"get_exp_type", Record::GET_EXP_TYPE},
    {u"consume_meal_in_battle_on_ride", Record::CON_MEAL_BATTLE_ON_RIDE},
    {u"consume_meal_in_normal_on_ride", Record::CON_MEAL_NORMAL_ON_RIDE},
    {u"food", Record::FOOD_CLASS_ID},
    {u"hungry_limit", Record::HUNGRY_LIMIT},
    {u"soulshot_count", Record::SOULSHOT_COUNT},
    {u"spiritshot_count", Record::SPIRITSHOT_COUNT},
    {u"is_penalty", Record::IS_PENALTY},
};

PetDataSheetTable gPetDataSheet;

PetDataSheet::Record::Record() { Init(); }

void PetDataSheet::Record::Init() {
    mMaxMeal = 128;
    mConsumeMealInBattle = 2;
    mConsumeMealInNormal = 0;
    mLevel = 0;
    mExp = 0;
    mdOrgPAttack = 2.1;
    mdOrgPDefend = 11.1;
    mdOrgMAttack = 1.4;
    mdOrgMDefend = 8.1;
    mdOrgHP = 19.9;
    mdOrgMP = 40.0;
    mdOrgHPRegen = 2.0;
    mdOrgMPRegen = 0.9;
    for (auto& mode : mSpeedOnRide) {
        for (std::int32_t& speed : mode) {
            speed = 1;
        }
    }
    mGetExpType = 0;
    mdAttackSpeedOnRide = 1.0;
    mdPAttackOnRide = 1.0;
    mdMAttackOnRide = 1.0;
    mdMaxHPOnRide = 1.0;
    mdMaxMPOnRide = 1.0;
    mConsumeMealInBattleOnRide = 1;
    mConsumeMealInNormalOnRide = 1;
    for (std::int32_t& classId : m_nFoodClassId) {
        classId = 0;
    }
    m_nHungryLimit = 0;
    m_nSoulShotCount = 0;
    m_nSpiritShotCount = 0;
    m_bIsPenalty = true;
}

void PetDataSheet::Record::Set(Attr attr, double value) {
    const auto integer = static_cast<std::int64_t>(value);
    const auto integer32 = static_cast<std::int32_t>(integer);

    switch (attr) {
        case LEVEL:
            mLevel = integer32;
            break;
        case MAX_MEAL:
            mMaxMeal = integer32;
            break;
        case EXP:
            mExp = integer;
            break;
        case CON_MEAL_BATTLE:
            mConsumeMealInBattle = integer32;
            break;
        case CON_MEAL_NORMAL:
            mConsumeMealInNormal = integer32;
            break;
        case ORG_PATTACK:
            mdOrgPAttack = value;
            break;
        case ORG_PDEFEND:
            mdOrgPDefend = value;
            break;
        case ORG_MATTACK:
            mdOrgMAttack = value;
            break;
        case ORG_MDEFEND:
            mdOrgMDefend = value;
            break;
        case ORG_HP:
            mdOrgHP = value;
            break;
        case ORG_MP:
            mdOrgMP = value;
            break;
        case ORG_HPREGEN:
            mdOrgHPRegen = value;
            break;
        case ORG_MPREGEN:
            mdOrgMPRegen = value;
            break;
        case ATTACK_SPEED_ON_RIDE:
            mdAttackSpeedOnRide = value;
            break;
        case PATTACK_ON_RIDE:
            mdPAttackOnRide = value;
            break;
        case MATTACK_ON_RIDE:
            mdMAttackOnRide = value;
            break;
        case MAX_HP_ON_RIDE:
            mdMaxHPOnRide = value;
            break;
        case MAX_MP_ON_RIDE:
            mdMaxMPOnRide = value;
            break;
        case GET_EXP_TYPE:
            mGetExpType = integer32;
            break;
        case CON_MEAL_BATTLE_ON_RIDE:
            mConsumeMealInBattleOnRide = integer32;
            break;
        case CON_MEAL_NORMAL_ON_RIDE:
            mConsumeMealInNormalOnRide = integer32;
            break;
        case HUNGRY_LIMIT:
            m_nHungryLimit = integer32;
            break;
        case SOULSHOT_COUNT:
            m_nSoulShotCount = integer32;
            break;
        case SPIRITSHOT_COUNT:
            m_nSpiritShotCount = integer32;
            break;
        case IS_PENALTY:
            m_bIsPenalty = integer == 1;
            break;
        case SPEED_ON_RIDE:
        case FOOD_CLASS_ID:
            break;
    }
}

void PetDataSheet::Record::Set(Attr attr, const std::vector<std::int64_t>& values) {
    L2_GUARD_FUNCTION();

    if (attr == SPEED_ON_RIDE) {
        // The generated grammar always supplies the first six entries. The
        // final pair was added later and remains optional in the current
        // body.
        mSpeedOnRide[0][0] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 0));
        mSpeedOnRide[1][0] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 1));
        mSpeedOnRide[0][1] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 2));
        mSpeedOnRide[1][1] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 3));
        mSpeedOnRide[0][2] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 4));
        mSpeedOnRide[1][2] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 5));
        if (values.size() > 6) {
            mSpeedOnRide[0][3] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 6));
        }
        if (values.size() > 7) {
            mSpeedOnRide[1][3] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, 7));
        }
        return;
    }

    if (attr == FOOD_CLASS_ID) {
        const std::size_t count = std::min<std::size_t>(values.size(), std::size(m_nFoodClassId));
        for (std::size_t index = 0; index < count; ++index) {
            m_nFoodClassId[index] = static_cast<std::int32_t>(runtime::CheckedVectorAt(values, index));
        }
    }
}

PetDataSheet::PetDataSheet() :
    mNpcClassId(-1), m_Index(0), m_itemClassID(-1), m_bSyncLevel(false) {}

PetDataSheet::~PetDataSheet() = default;

PetDataSheet::Record* PetDataSheet::GetRecord(std::int32_t level) {
    if (static_cast<std::uint32_t>(level) < kRecordCount) {
        return &mRecords[level];
    }
    return mRecords;
}

AttrTypeInfo* PetDataSheet::GetTypeInfo(const char16_t* symbol) {
    for (AttrTypeInfo& info : msTable) {
        if (EqualsAsciiCaseInsensitive(info.mSymbol, symbol)) {
            return &info;
        }
    }
    return nullptr;
}

void PetDataSheet::ClearEvolutionInfo() { m_evolutionData.clear(); }

void PetDataSheet::SetEvolutionInfo(const std::vector<std::int32_t>& evolutionData) {
    ClearEvolutionInfo();
    for (const std::int32_t value : evolutionData) {
        m_evolutionData.push_back(value);
    }
}

void PetDataSheet::SetName(const char16_t* name) { mName.assign(name); }

std::int32_t PetDataSheet::CheckEvolutionID(std::int32_t level) {
    for (std::size_t position = m_evolutionData.size(); position > 0; --position) {
        if (level >= runtime::CheckedVectorAt(m_evolutionData, position - 1)) {
            return static_cast<std::int32_t>(position);
        }
    }
    return 0;
}

void PetDataSheet::Verify() {
    // const std::wstring name = target_utf16::ToHostCodeUnits(mName.c_str())
    const std::string name = Utf16ToUtf8(mName);
    for (std::int32_t level = 1; level < 121; ++level) {
        const Record& current = mRecords[level];
        if (current.mLevel == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's data is required.",
            //     name.c_str(),
            //     level)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's data is required.",
            //     name.c_str(),
            //     level)
            L2_LOG_ERROR(
                Log, "Pet '%s' has no data for level %d; comparisons for this level are skipped.", name.c_str(), level
            );
            continue;
        }

        const Record& next = mRecords[level + 1];
        if (next.mLevel == 0) {
            continue;
        }
        if (next.mExp <= current.mExp) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's exp(=%I64d) is larger than level %d's.",
            //     name.c_str(),
            //     level,
            //     static_cast<long long>(current.mExp),
            //     level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's exp(=%I64d) is larger than level %d's.",
            //     name.c_str(),
            //     level,
            //     static_cast<long long>(current.mExp),
            //     level + 1)
            L2_LOG_ERROR(
                Log, "Pet '%s' experience does not increase from level %d (%lld) to level %d (%lld); values are retained.",
                name.c_str(), level, static_cast<long long>(current.mExp), level + 1, static_cast<long long>(next.mExp)
            );
        }
        if (next.mdOrgHP < current.mdOrgHP) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's OrgHP(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgHP, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's OrgHP(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgHP, level + 1)
            L2_LOG_ERROR(
                Log, "Pet '%s' base HP decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgHP, level + 1, next.mdOrgHP
            );
        }
        if (next.mdOrgMP < current.mdOrgMP) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's orgMP(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMP, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's orgMP(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMP, level + 1)
            L2_LOG_ERROR(
                Log, "Pet '%s' base MP decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgMP, level + 1, next.mdOrgMP
            );
        }
        if (next.mdOrgPAttack < current.mdOrgPAttack) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's orgPAttack(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgPAttack, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's orgPAttack(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgPAttack, level + 1)
            L2_LOG_ERROR(
                Log,
                "Pet '%s' base physical attack decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgPAttack, level + 1, next.mdOrgPAttack
            );
        }
        if (next.mdOrgMAttack < current.mdOrgMAttack) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's orgMAttack(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMAttack, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's orgMAttack(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMAttack, level + 1)
            L2_LOG_ERROR(
                Log, "Pet '%s' base magic attack decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgMAttack, level + 1, next.mdOrgMAttack
            );
        }
        if (next.mdOrgPDefend < current.mdOrgPDefend) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's orgPDefend(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgPDefend, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's orgPDefend(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgPDefend, level + 1)
            L2_LOG_ERROR(
                Log,
                "Pet '%s' base physical defence decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgPDefend, level + 1, next.mdOrgPDefend
            );
        }
        if (next.mdOrgMDefend < current.mdOrgMDefend) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's orgMDefend(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMDefend, level + 1)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's orgMDefend(=%0.1f) is larger than level %d's.",
            //     name.c_str(), level, current.mdOrgMDefend, level + 1)
            L2_LOG_ERROR(
                Log, "Pet '%s' base magic defence decreases from level %d (%.17g) to level %d (%.17g); values are retained.",
                name.c_str(), level, current.mdOrgMDefend, level + 1, next.mdOrgMDefend
            );
        }
        if (static_cast<std::uint32_t>(current.m_nHungryLimit) > 99) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Warning : pet=[%s], Level %d's hungry_limit(=%d) is not in rage(0~99).",
            //     name.c_str(), level, current.m_nHungryLimit)
            // L2_LOG_ERROR(Log,
            //     L"Warning : pet=[%s], Level %d's hungry_limit(=%d) is not in rage(0~99).",
            //     name.c_str(), level, current.m_nHungryLimit)
            L2_LOG_ERROR(
                Log, "Pet '%s' hungry_limit is %d at level %d; expected 0 through 99, the supplied value is retained.",
                name.c_str(), current.m_nHungryLimit, level
            );
        }
    }
}

PetDataSheetTable::PetDataSheetTable() { RemoveAll(); }

PetDataSheetTable::~PetDataSheetTable() {
    delete m_pDefaultPetDataSheet;
    m_pDefaultPetDataSheet = nullptr;
}

PetDataSheetTable& PetDataSheetTable::Inst() { return gPetDataSheet; }

bool PetDataSheetTable::Load(wchar_t* fileName) {
    PetDataParser parser;
    CSecureStream input(fileName, true);
    if (input.fail()) {
        const wchar_t* const failedFileName = fileName != nullptr ? fileName : L"PetData";
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file[%s] failed",
        //     L"PetDataSheet.cpp",
        //     0x23b,
        //     failedFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file[%s] failed",
        //     L"PetDataSheet.cpp",
        //     0x23b,
        //     failedFileName)
        L2_LOG_ERROR(Log, "Cannot read pet data '%s'; loading fails.", WideToUtf8(failedFileName).c_str());
        return false;
    }

    PetDataLexer lexer;
    if (!lexer.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // std::wprintf(
        //     L"[%ls][%d] PetDataLexer::create failed. Can't open %ls\n",
        //     L"PetDataSheet.cpp",
        //     0x244,
        //     fileName)
        L2_LOG_ERROR(
            Log, "Cannot initialize the lexer for pet data '%s'; loading fails.",
            WideToUtf8(fileName != nullptr ? fileName : L"<null>").c_str()
        );
        return false;
    }
    if (!parser.Create(&lexer, this)) {
        // std::wprintf(
        //     L"[%ls][%d] PetDataParser::create failed.\n",
        //     L"PetDataSheet.cpp",
        //     0x24b)
        L2_LOG_ERROR(
            Log, "Cannot initialize the parser for pet data '%s'; loading fails.",
            WideToUtf8(fileName != nullptr ? fileName : L"<null>").c_str()
        );
        return false;
    }
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing petdata file failed",
        //     L"PetDataSheet.cpp",
        //     0x252)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing petdata file failed",
        //     L"PetDataSheet.cpp",
        //     0x252)
        L2_LOG_ERROR(
            Log, "Cannot parse pet data '%s' at line %d; loading fails and already inserted records are not rolled back.",
            WideToUtf8(fileName != nullptr ? fileName : L"<null>").c_str(), lexer.yylineno
        );
        return false;
    }
    return true;
}

void PetDataSheetTable::Insert(PetDataSheet* dataSheet) {
    if (dataSheet == nullptr) {
        return;
    }
    m_PetDataPetIdMap[dataSheet->m_Index] = dataSheet;
    m_PetDataNpcClassIdMap[dataSheet->mNpcClassId] = dataSheet;
    m_PetDataItemClassIdMap[dataSheet->m_itemClassID] = dataSheet;
    m_PetDataNameMap[dataSheet->mName] = dataSheet;
}

void PetDataSheetTable::Remove(std::int32_t petIndex) {
    PetDataSheet* const dataSheet = GetPetDataSheetByIndex(petIndex);
    if (dataSheet == nullptr) {
        return;
    }
    m_PetDataPetIdMap.erase(dataSheet->m_Index);
    m_PetDataNpcClassIdMap.erase(dataSheet->mNpcClassId);
    m_PetDataItemClassIdMap.erase(dataSheet->m_itemClassID);
    m_PetDataNameMap.erase(dataSheet->mName);
    delete dataSheet;
}

void PetDataSheetTable::RemoveAll() {
    while (!m_PetDataPetIdMap.empty()) {
        Remove(m_PetDataPetIdMap.begin()->second->m_Index);
    }
    m_pDefaultPetDataSheet = new PetDataSheet;
    Insert(m_pDefaultPetDataSheet);
}

void PetDataSheetTable::Verify() {
    for (const auto& entry : m_PetDataPetIdMap) {
        if (entry.second->m_Index > 0) {
            entry.second->Verify();
        }
    }
}

PetDataSheet* PetDataSheetTable::FindDataSheet(std::int32_t npcClassId) {
    const auto position = m_PetDataNpcClassIdMap.find(npcClassId);
    return position == m_PetDataNpcClassIdMap.end() ? nullptr : position->second;
}

PetDataSheet* PetDataSheetTable::FindDataSheet(const char16_t* name) {
    const auto position = m_PetDataNameMap.find(std::u16string(name));
    return position == m_PetDataNameMap.end() ? nullptr : position->second;
}

PetDataSheet* PetDataSheetTable::GetPetDataSheetByIndex(std::int32_t index) {
    const auto position = m_PetDataPetIdMap.find(index);
    return position == m_PetDataPetIdMap.end() ? m_pDefaultPetDataSheet : position->second;
}

std::int32_t PetDataSheetTable::NpcClassIdFromIndex(std::int32_t index) {
    return GetPetDataSheetByIndex(index)->mNpcClassId;
}

std::int32_t PetDataSheetTable::ItemClassIdFromIndex(std::int32_t index) {
    return GetPetDataSheetByIndex(index)->m_itemClassID;
}

std::int32_t PetDataSheetTable::NpcClassIdFromItemClassID(std::int32_t itemClassId) {
    const auto position = m_PetDataItemClassIdMap.find(itemClassId);
    return position == m_PetDataItemClassIdMap.end() ? -1 : position->second->mNpcClassId;
}

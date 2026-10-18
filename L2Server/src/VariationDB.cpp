#include "VariationDB.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/TextRuntime.h"

#include "GameEventSystem.h"
#include "Item.h"
#include "ObjectDB.h"
#include "OptionDB.h"
#include "ParserVariation.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <cstdint>

VariationDB g_VariationDB;

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     OptionGroup::kMemoryClassId ==
//     L2ServerMemory::kOptionGroupClassId)
// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     Variation::kMemoryClassId ==
//     L2ServerMemory::kVariationClassId)
// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     VariationDB::kMemoryClassId ==
//     L2ServerMemory::kVariationDBClassId)

bool VariationKey::operator<(const VariationKey& other) const noexcept {
    if (intenItemClassID != other.intenItemClassID) {
        return intenItemClassID < other.intenItemClassID;
    }
    return weaponType < other.weaponType;
}

bool VariationKey::operator==(const VariationKey& other) const noexcept {
    return intenItemClassID == other.intenItemClassID && weaponType == other.weaponType;
}

VariationKey::WeaponType VariationKey::GetWeaponType(CItem* item) {
    // Current L2Server 0x00c3f858.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return MAX;
    }
    return item->m_const->m_bIsMagicWeapon ? MAGE : WARRIOR;
}

bool CommissionKey::operator<(const CommissionKey& other) const noexcept {
    if (intenItemClassID != other.intenItemClassID) {
        return intenItemClassID < other.intenItemClassID;
    }
    return itemGroupNo < other.itemGroupNo;
}

bool CommissionKey::operator==(const CommissionKey& other) const noexcept {
    return intenItemClassID == other.intenItemClassID && itemGroupNo == other.itemGroupNo;
}

// Current L2Server 0x00cc202c.
L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(OptionGroup)

OptionGroup::OptionGroup() {}

OptionGroup::~OptionGroup() = default;

// Current L2Server 0x00cc2494.
L2_DEFINE_OBJECT_RELEASE(OptionGroup)

void OptionGroup::InsertOption(int optionNo, double chance) {
    // Current L2Server 0x00c4035c narrows the cumulative chance to float.
    L2_GUARD_FUNCTION();
    optionList.push_back(OptionElement{static_cast<float>(chance), optionNo});
}

Option* OptionGroup::GetOption() {
    // Current L2Server 0x00c40ef4.
    L2_GUARD_FUNCTION();
    const float random = static_cast<float>(genrand64_real1() * 100.0);
    float lastChance = 0.0F;
    for (const OptionElement& element : optionList) {
        lastChance = element.m_Chance;
        if (random <= element.m_Chance) {
            return g_VariationDB.GetOptionBy(element.m_OptionNo, true);
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid option chance rate or has no option [ %d %%, %d %%] ",
    //     L"VariationDB.cpp",
    //     0x55,
    //     static_cast<double>(random),
    //     static_cast<double>(lastChance))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid option chance rate or has no option [ %d %%, %d %%] ",
    //     L"VariationDB.cpp",
    //     0x55,
    //     static_cast<double>(random),
    //     static_cast<double>(lastChance))
    L2_LOG_ERROR(
        Log, "No augmentation option covers random roll %.9g%% (last threshold %.9g%%, %zu options); no option is returned.",
        static_cast<double>(random), static_cast<double>(lastChance), static_cast<std::size_t>(optionList.size())
    );
    return nullptr;
}

bool OptionGroup::IsMaxOptionList() {
    // Current L2Server 0x00cc221c: despite its name, true means there is room.
    L2_GUARD_FUNCTION();
    return optionList.size() < 500;
}

int OptionGroup::GetSizeOfOptionList() {
    // Current L2Server 0x00cc22bc.
    L2_GUARD_FUNCTION();
    return static_cast<int>(optionList.size());
}

// Current L2Server 0x00cc20b0.
L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(Variation)

Variation::Variation() {}

Variation::~Variation() = default;

// Current L2Server 0x00cc2644.
L2_DEFINE_OBJECT_RELEASE(Variation)

void Variation::InsertOptionGroup(OptionGroup* optionGroup, double chance, VariationType variationType) {
    // Current L2Server 0x00c40420.
    L2_GUARD_FUNCTION();
    OptionGroupElement element;
    element.pOptionGroup = optionGroup;
    element.m_Chance = static_cast<float>(chance);
    if (variationType == VARI_NORMAL) {
        normalOptionGroupList.push_back(element);
    } else {
        randomOptionGroupList.push_back(element);
    }
}

Option* Variation::GetOptionImpl(OptionGroupList& optionGroupList) {
    // Current L2Server 0x00c41028.
    L2_GUARD_FUNCTION();
    const float random = static_cast<float>(genrand64_real1() * 100.0);
    float lastChance = 0.0F;
    for (OptionGroupElement& element : optionGroupList) {
        lastChance = element.m_Chance;
        if (random <= element.m_Chance) {
            if (element.pOptionGroup != nullptr) {
                return element.pOptionGroup->GetOption();
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid option group chance rate or has no option group [%d %%, %d %%]",
            //     L"VariationDB.cpp",
            //     0x83,
            //     static_cast<double>(random),
            //     static_cast<double>(lastChance))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid option group chance rate or has no option group [%d %%, %d %%]",
            //     L"VariationDB.cpp",
            //     0x83,
            //     static_cast<double>(random),
            //     static_cast<double>(lastChance))
            L2_LOG_ERROR(
                Log, "Selected augmentation option group is null at roll %.9g%%, threshold %.9g%%; no option is returned.",
                static_cast<double>(random), static_cast<double>(lastChance)
            );
            return nullptr;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid option group chance rate or has no option group [%d %% , %d %%]",
    //     L"VariationDB.cpp",
    //     0x8a,
    //     static_cast<double>(random),
    //     static_cast<double>(lastChance))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid option group chance rate or has no option group [%d %% , %d %%]",
    //     L"VariationDB.cpp",
    //     0x8a,
    //     static_cast<double>(random),
    //     static_cast<double>(lastChance))
    L2_LOG_ERROR(
        Log,
        "No augmentation option group covers random roll %.9g%% (last threshold %.9g%%, %zu groups); no option is returned.",
        static_cast<double>(random), static_cast<double>(lastChance), static_cast<std::size_t>(optionGroupList.size())
    );
    return nullptr;
}

Option* Variation::GetNormalOption() {
    // Current L2Server 0x00c4119c.
    L2_GUARD_FUNCTION();
    return GetOptionImpl(normalOptionGroupList);
}

Option* Variation::GetRandomOption() {
    // Current L2Server 0x00c41244.
    L2_GUARD_FUNCTION();
    return GetOptionImpl(randomOptionGroupList);
}

VariationDB::VariationDB() {
    // Current L2Server 0x00c406a4 clears only this first set.
    intenSet.clear();
}

VariationDB::~VariationDB() = default;

// Current L2Server 0x00c40880.
L2_DEFINE_OBJECT_RELEASE(VariationDB)

void VariationDB::RegisterIntenSet(int intensiveItemClassID) {
    // Current L2Server 0x00c3fcf4.
    L2_GUARD_FUNCTION();
    intenSet.insert(intensiveItemClassID);
}

void VariationDB::RegisterExceptionItem(int itemClassID) {
    // Current L2Server 0x00c3fda4.
    L2_GUARD_FUNCTION();
    exceptionSet.insert(itemClassID);
}

bool VariationDB::HasIntenItem(int intensiveItemClassID) const {
    return intenSet.find(intensiveItemClassID) != intenSet.end();
}

bool VariationDB::IsExceptionItem(int itemClassID) const { return exceptionSet.find(itemClassID) != exceptionSet.end(); }

bool VariationDB::IsValidCandidateItem(std::uint32_t itemClassID) {
    // Current L2Server 0x00c3fe54.
    L2_GUARD_FUNCTION();
    return itemToGroupMap.find(itemClassID) != itemToGroupMap.end();
}

std::uint16_t VariationDB::GetItemGroupNo(std::uint32_t itemClassID) {
    // Current L2Server 0x00c3ff30.
    L2_GUARD_FUNCTION();
    const auto found = itemToGroupMap.find(itemClassID);
    if (found == itemToGroupMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "GetItemGroupNo failed [IC.%d]",
        //     itemClassID)
        // L2_LOG_ERROR(Log,
        //     "GetItemGroupNo failed [IC.%d]",
        //     itemClassID)
        L2_LOG_ERROR(
            Log, "No augmentation item group is registered for item class %u; returning group 0.",
            static_cast<unsigned>(itemClassID)
        );
        return 0;
    }
    return found->second;
}

void VariationDB::RegisterItemToGroup(std::uint32_t itemClassID, std::uint16_t itemGroupNo) {
    // Current L2Server 0x00c40038 uses insert, not assignment.
    L2_GUARD_FUNCTION();
    itemToGroupMap.emplace(itemClassID, itemGroupNo);
}

void VariationDB::RegisterCommissionInfo(CommissionKey key, CommissionInfo info) {
    // Current L2Server 0x00c40108 uses insert, not assignment.
    L2_GUARD_FUNCTION();
    keyToCommissionMap.emplace(key, info);
}

ItemOption VariationDB::GetNewBlankItemOption() { return ItemOption(ItemOptionKey{}); }

ItemOption VariationDB::CreateItemOption(Option* normalOption, Option* randomOption) {
    // Current L2Server 0x00c40500.
    L2_GUARD_FUNCTION();
    ItemOption result = GetNewBlankItemOption();
    if (normalOption == nullptr || randomOption == nullptr) {
        return result;
    }

    result.itemOptionKey.variationOption1 = normalOption->optionNo;
    result.itemOptionKey.variationOption2 = randomOption->optionNo;
    result.baseValue = normalOption->baseValue + randomOption->baseValue;
    result.skillData = randomOption->skillData;
    result.skillData.acquiredSkills.insert(
        result.skillData.acquiredSkills.end(), normalOption->skillData.acquiredSkills.begin(),
        normalOption->skillData.acquiredSkills.end()
    );
    result.skillData.effectSkills.insert(
        result.skillData.effectSkills.end(), normalOption->skillData.effectSkills.begin(),
        normalOption->skillData.effectSkills.end()
    );
    return result;
}

void VariationDB::Load() {
    // Current L2Server 0x00c40a24.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"Loading VariationDB")
    // L2_LOG_INFO(Log, L"Loading VariationDB")
    L2_LOG_INFO(Log, "Loading variationdata.txt.");

    const std::int64_t memoryBefore = g_Memory.GetTotalAllocSize();
    const std::uint32_t tickBefore = runtime::GetTickCount();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\variationdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/variationdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from variationdata file failed",
        //     L"VariationDB.cpp",
        //     0xb1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from variationdata file failed",
        //     L"VariationDB.cpp",
        //     0xb1)
        L2_LOG_ERROR(Log, "Cannot read variationdata.txt; augmentation-data loading stops.");
        return;
    }

    CParserForVariation parser;
    if (!parser.Create(this)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] optionParser.create failed",
        //     L"VariationDB.cpp",
        //     0xb9)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] optionParser.create failed",
        //     L"VariationDB.cpp",
        //     0xb9)
        L2_LOG_ERROR(Log, "Cannot initialize the variationdata.txt parser; augmentation-data loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing variationdata file failed after line[%d]",
        //     L"VariationDB.cpp",
        //     0xc4,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing variationdata file failed after line[%d]",
        //     L"VariationDB.cpp",
        //     0xc4,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse variationdata.txt at script line %lld; already processed augmentation data is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }

    const std::uint32_t tickAfter = runtime::GetTickCount();
    const std::int64_t memoryAfter = g_Memory.GetTotalAllocSize();
    // Log.Add(
    //     LOG_NORMAL,
    //     "Complete to load VariationDB [%u KB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 10,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    // L2_LOG_INFO(Log,
    //     "Complete to load VariationDB [%u KB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 10,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    L2_LOG_INFO(
        Log, "Variation data load attempt finished in %u ms; allocated memory before %lld bytes, after %lld bytes.",
        static_cast<unsigned>(tickAfter - tickBefore), static_cast<long long>(memoryBefore),
        static_cast<long long>(memoryAfter)
    );
}

int VariationDB::GetOptionNoBy(const char16_t* optionName, bool logError) {
    const Option* const option =
        OptionDB::inst().FindByOptionName(std::u16string(optionName == nullptr ? u"" : optionName), logError);
    return option == nullptr ? -1 : option->optionNo;
}

Option* VariationDB::GetOptionBy(int optionNo, bool logError) { return OptionDB::inst().FindByOptionNo(optionNo, logError); }

ItemOption VariationDB::GetItemOptionOnLoad(ItemOptionKey optionKey) {
    // Current L2Server 0x00c40d54.
    L2_GUARD_FUNCTION();
    if (optionKey.variationOption1 < 1 || optionKey.variationOption2 < 1) {
        return GetNewBlankItemOption();
    }

    Option* const normalOption = GetOptionBy(optionKey.variationOption1, true);
    Option* const randomOption = GetOptionBy(optionKey.variationOption2, true);
    if (normalOption == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] normalOption is null,  optionKey[%d, %d]",
        //     L"VariationDB.cpp",
        //     0x157,
        //     optionKey.variationOption1,
        //     optionKey.variationOption2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] normalOption is null,  optionKey[%d, %d]",
        //     L"VariationDB.cpp",
        //     0x157,
        //     optionKey.variationOption1,
        //     optionKey.variationOption2)
        L2_LOG_ERROR(
            Log, "Cannot restore augmentation (%d, %d): normal option is missing; returning an empty augmentation.",
            optionKey.variationOption1, optionKey.variationOption2
        );
    } else if (randomOption != nullptr) {
        return CreateItemOption(normalOption, randomOption);
    }

    if (randomOption == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] randomOption is null,  optionKey[%d, %d]",
        //     L"VariationDB.cpp",
        //     0x15d,
        //     optionKey.variationOption1,
        //     optionKey.variationOption2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] randomOption is null,  optionKey[%d, %d]",
        //     L"VariationDB.cpp",
        //     0x15d,
        //     optionKey.variationOption1,
        //     optionKey.variationOption2)
        L2_LOG_ERROR(
            Log, "Cannot restore augmentation (%d, %d): random option is missing; returning an empty augmentation.",
            optionKey.variationOption1, optionKey.variationOption2
        );
    }
    return GetNewBlankItemOption();
}

ItemOptionKey VariationDB::GetItemOptionKeyOnCreate(VariationKey variationKey) {
    // Current L2Server 0x00c412ec.
    L2_GUARD_FUNCTION();
    const auto found = keyToVariationMap.find(variationKey);
    const wchar_t* const weaponType = variationKey.weaponType == VariationKey::MAGE ? L"mage_weapon" : L"warrior_weapon";
    if (found == keyToVariationMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x185,
        //     variationKey.intenItemClassID,
        //     weaponType)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x185,
        //     variationKey.intenItemClassID,
        //     weaponType)
        L2_LOG_ERROR(
            Log,
            "Cannot create augmentation for catalyst item class %d, weapon type %d (%s): no variation is registered; "
            "returning an empty option key.",
            variationKey.intenItemClassID, static_cast<int>(variationKey.weaponType), WideToUtf8(weaponType).c_str()
        );
        return ItemOptionKey{};
    }

    Variation* const variation = found->second;
    Option* const normalOption = variation->GetNormalOption();
    Option* const randomOption = variation->GetRandomOption();
    if (normalOption == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x174,
        //     variationKey.intenItemClassID,
        //     weaponType)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x174,
        //     variationKey.intenItemClassID,
        //     weaponType)
        L2_LOG_ERROR(
            Log,
            "Cannot create augmentation for catalyst item class %d, weapon type %d (%s): normal option selection failed; "
            "returning an empty option key.",
            variationKey.intenItemClassID, static_cast<int>(variationKey.weaponType), WideToUtf8(weaponType).c_str()
        );
    } else if (randomOption != nullptr) {
        ItemOptionKey result;
        result.variationOption1 = normalOption->optionNo;
        result.variationOption2 = randomOption->optionNo;
        return result;
    }

    if (randomOption == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x17b,
        //     variationKey.intenItemClassID,
        //     weaponType)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] normalOption is null,  key[%d, %s]",
        //     L"VariationDB.cpp",
        //     0x17b,
        //     variationKey.intenItemClassID,
        //     weaponType)
        L2_LOG_ERROR(
            Log,
            "Cannot create augmentation for catalyst item class %d, weapon type %d (%s): random option selection failed; "
            "returning an empty option key.",
            variationKey.intenItemClassID, static_cast<int>(variationKey.weaponType), WideToUtf8(weaponType).c_str()
        );
    }
    return ItemOptionKey{};
}

CommissionInfo VariationDB::GetCommissionInfo(std::uint32_t candidateItemClassID, std::uint32_t intensiveItemClassID) {
    // Current L2Server 0x00c414e4.
    L2_GUARD_FUNCTION();
    const CommissionKey key(static_cast<int>(intensiveItemClassID), GetItemGroupNo(candidateItemClassID));
    const auto found = keyToCommissionMap.find(key);
    if (found == keyToCommissionMap.end()) {
        return CommissionInfo{};
    }

    const CommissionInfo& original = found->second;
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_VARIATION);
    if (event == nullptr || !event->InEventTime()) {
        return original;
    }

    const auto* const variationEvent = static_cast<const CEvent_VariationEvent*>(event);
    const int cancelAdena = runtime::Win64CvttDoubleToInt32(
        (static_cast<double>(variationEvent->nCancelFeePercentage) / 100.0) * static_cast<double>(original.cancelAdena)
    );
    const int itemCount = runtime::Win64CvttDoubleToInt32(
        (static_cast<double>(variationEvent->nGemStoneCountPercentage) / 100.0) * static_cast<double>(original.itemCount) +
        0.5
    );
    return CommissionInfo(cancelAdena, original.itemClassID, itemCount);
}

void VariationDB::RemoveAll() {
    intenSet.clear();
    exceptionSet.clear();
    keyToVariationMap.clear();
    keyToCommissionMap.clear();
    itemToGroupMap.clear();
}

#include "PredefinedListDB.h"
#include "Shared/src/TextRuntime.h"

#include "Object.h"
#include "ObjectDB.h"
#include "PredefinedListParser.h"
#include "Shared/src/MemorySub.h"
#include "Skill.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <cstdint>
#include <string>

// static_assert(L2ServerMemory::kPredefinedSkillListClassId == 238)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PredefinedSkillList) == 238);
// static_assert(L2ServerMemory::kPredefinedItemListClassId == 239)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PredefinedItemList) == 239);
// static_assert(L2ServerMemory::kPredefinedTerritoryListClassId == 240)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::PredefinedTerritoryList) == 240);

PredefinedSkillList::PredefinedSkillList(int predefinedListId) :
    BasePredefinedList(predefinedListId, PREDEFINED_LIST_SKILL) {}

PredefinedSkillList::~PredefinedSkillList() = default;

L2_DEFINE_OBJECT_TYPE_OUT_OF_LINE(PredefinedSkillList)

void PredefinedSkillList::AddName(const char16_t* name) { m_vPredefinedSkillNameList.push_back(std::u16string(name)); }

PredefinedItemList::PredefinedItemList(int predefinedListId) :
    BasePredefinedList(predefinedListId, PREDEFINED_LIST_ITEM) {}

PredefinedItemList::~PredefinedItemList() = default;

L2_DEFINE_OBJECT_TYPE_OUT_OF_LINE(PredefinedItemList)

void PredefinedItemList::AddName(const char16_t* name) { m_vPredefinedItemNameList.push_back(std::u16string(name)); }

PredefinedTerritoryList::PredefinedTerritoryList(int predefinedListId) :
    BasePredefinedList(predefinedListId, PREDEFINED_LIST_TERRITORY) {}

PredefinedTerritoryList::~PredefinedTerritoryList() = default;

L2_DEFINE_OBJECT_TYPE_OUT_OF_LINE(PredefinedTerritoryList)

PredefinedListDB::PredefinedListDB() :
    m_PredefinedList_pch_file(nullptr),
    m_bOnlyPrecompiledHeader(false) {
    L2_WRITE_GUARD(m_lock);
    m_mapPredefinedList.clear();
    m_mapPredefinedListNameMap.clear();
}

PredefinedListDB::~PredefinedListDB() { m_PredefinedList_pch_file = nullptr; }

BasePredefinedList* PredefinedListDB::FindByPredefinedListID(int predefinedListId, bool logError) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(m_lock);

    BasePredefinedList* predefinedList = nullptr;
    const auto found = m_mapPredefinedList.find(predefinedListId);
    if (found == m_mapPredefinedList.end()) {
        if (logError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid PredefinedListID[%d]",
            //     L"PredefinedListDB.cpp",
            //     0x65,
            //     predefinedListId)
            // Noise: The null-result diagnostic below already reports this missing ID once.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid PredefinedListID[%d]",
            //     L"PredefinedListDB.cpp",
            //     0x65,
            //     predefinedListId)
        }
    } else {
        predefinedList = found->second;
    }

    if (predefinedList == nullptr && logError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not Exist PredefinedList Data - PredefinedListID[%d]",
        //     L"PredefinedListDB.cpp",
        //     0x6f,
        //     predefinedListId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Not Exist PredefinedList Data - PredefinedListID[%d]",
        //     L"PredefinedListDB.cpp",
        //     0x6f,
        //     predefinedListId)
        L2_LOG_ERROR(
            Log, "Cannot find predefined list %d: %s; no list is returned.", predefinedListId,
            found == m_mapPredefinedList.end() ? "ID is not registered" : "registered entry is null"
        );
    }
    return predefinedList;
}

void PredefinedListDB::RegisterNoToPredefinedList(int predefinedListId, BasePredefinedList* predefinedList) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    m_mapPredefinedList.emplace(predefinedListId, predefinedList);
}

void PredefinedListDB::RegisterNameToPredefinedList(
    const std::u16string& predefinedListName, BasePredefinedList* predefinedList
) {
    L2_GUARD_FUNCTION();
    m_mapPredefinedListNameMap.emplace(predefinedListName, predefinedList->GetPredefinedListID());
}

void PredefinedListDB::Load() {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, "Loading PredefinedListDB")
    // Noise: LoadData already announces this stage. The loader keeps failure and completion messages.
    // L2_LOG_INFO(Log, "Loading PredefinedListDB")

    const std::int64_t memoryBefore = g_Memory.GetTotalAllocSize();
    const std::uint32_t tickBefore = runtime::GetTickCount();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\predefinedlistdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/predefinedlistdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from PredefinedListdata file failed",
        //     L"PredefinedListDB.cpp",
        //     0x123)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from PredefinedListdata file failed",
        //     L"PredefinedListDB.cpp",
        //     0x123)
        L2_LOG_ERROR(Log, "Cannot read predefinedlistdata.txt; list loading skipped and existing lists retained.");
        return;
    }

    CParserForPredefinedList parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PredefinedListParser.create failed",
        //     L"PredefinedListDB.cpp",
        //     300)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] PredefinedListParser.create failed",
        //     L"PredefinedListDB.cpp",
        //     300)
        L2_LOG_ERROR(
            Log, "Cannot initialize the predefinedlistdata.txt parser; list loading skipped and existing lists retained."
        );
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = nullptr;
    // if (parser.yyparse() != 0) {
    const int parseResult = parser.yyparse();
    if (parseResult != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing PredefinedListdata file failed after line[%d]",
        //     L"PredefinedListDB.cpp",
        //     0x137,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing PredefinedListdata file failed after line[%d]",
        //     L"PredefinedListDB.cpp",
        //     0x137,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Parsing predefinedlistdata.txt stopped at line %lld; previously registered lists are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }

    const std::uint32_t tickAfter = runtime::GetTickCount();
    const std::int64_t memoryAfter = g_Memory.GetTotalAllocSize();
    // Log.Add(
    //     LOG_NORMAL,
    //     "Complete to load PredefinedListDB [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    // L2_LOG_INFO(Log,
    //     "Complete to load PredefinedListDB [%u MB, %ds]",
    //     static_cast<unsigned>(memoryAfter - memoryBefore) >> 20,
    //     static_cast<int>(tickAfter - tickBefore) / 1000)
    L2_LOG_INFO(
        Log,
        "Finished processing predefinedlistdata.txt in %u ms (parser result %d, %d syntax errors, allocator change %+.3f "
        "MiB).",
        static_cast<unsigned int>(tickAfter - tickBefore), parseResult, parser.yyerrorcount,
        (static_cast<double>(memoryAfter) - static_cast<double>(memoryBefore)) / 1048576.0
    );
}

bool PredefinedListDB::VerifyPredefinedListDB() {
    L2_READ_GUARD(m_lock);

    for (const auto& entry : m_mapPredefinedList) {
        BasePredefinedList* const base = entry.second;
        if (base == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid PredefinedList Data",
            //     L"PredefinedListDB.cpp",
            //     0xd6)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid PredefinedList Data",
            //     L"PredefinedListDB.cpp",
            //     0xd6)
            L2_LOG_ERROR(
                Log, "Cannot validate predefined list %d: the registered entry is null; validation stops.", entry.first
            );
            return false;
        }

        if (base->GetPredefinedListType() == PREDEFINED_LIST_SKILL) {
            auto* const skillList = dynamic_cast<PredefinedSkillList*>(base);
            if (skillList == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] PredefinedSkillList* is nullptr",
                //     L"PredefinedListDB.cpp",
                //     0xe3)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] PredefinedSkillList* is nullptr",
                //     L"PredefinedListDB.cpp",
                //     0xe3)
                L2_LOG_ERROR(
                    Log, "Cannot validate predefined skill list %d: its object has an incompatible type; validation stops.",
                    entry.first
                );
                return false;
            }
            for (const std::u16string& skillName : skillList->m_vPredefinedSkillNameList) {
                const std::wstring hostName = target_utf16::ToHostCodeUnits(skillName.c_str());
                if (g_SkillDB.GetSkillInfoByName(hostName.c_str()) == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Undefined Skill Name [%s]",
                    //     L"PredefinedListDB.cpp",
                    //     0xef,
                    //     hostName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Undefined Skill Name [%s]",
                    //     L"PredefinedListDB.cpp",
                    //     0xef,
                    //     hostName.c_str())
                    L2_LOG_ERROR(
                        Log, "Cannot validate predefined skill list %d: skill '%s' is not defined; validation stops.",
                        entry.first, Utf16ToUtf8(skillName.c_str()).c_str()
                    );
                    return false;
                }
            }
        } else if (base->GetPredefinedListType() == PREDEFINED_LIST_ITEM) {
            auto* const itemList = dynamic_cast<PredefinedItemList*>(base);
            if (itemList == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] PredefinedItemList* is nullptr",
                //     L"PredefinedListDB.cpp",
                //     0xfc)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] PredefinedItemList* is nullptr",
                //     L"PredefinedListDB.cpp",
                //     0xfc)
                L2_LOG_ERROR(
                    Log, "Cannot validate predefined item list %d: its object has an incompatible type; validation stops.",
                    entry.first
                );
                return false;
            }
            for (const std::u16string& itemName : itemList->m_vPredefinedItemNameList) {
                CObject* const object = g_ObjectDB.GetObjectByName(itemName.c_str());
                if (object == nullptr || !object->IsItem()) {
                    // const std::wstring hostName =
                    //     target_utf16::ToHostCodeUnits(itemName.c_str())
                    const std::string hostName = Utf16ToUtf8(itemName.c_str());
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Undefined Item Name [%s]",
                    //     L"PredefinedListDB.cpp",
                    //     0x109,
                    //     hostName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Undefined Item Name [%s]",
                    //     L"PredefinedListDB.cpp",
                    //     0x109,
                    //     hostName.c_str())
                    L2_LOG_ERROR(
                        Log, "Cannot validate predefined item list %d: '%s' does not identify an item; validation stops.",
                        entry.first, hostName.c_str()
                    );
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<std::int64_t>* PredefinedListDB::GetPredefinedSkillList(int predefinedListId) {
    BasePredefinedList* const base = PredefinedListDB::inst().FindByPredefinedListID(predefinedListId, true);
    auto* const skillList = dynamic_cast<PredefinedSkillList*>(base);
    if (skillList == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PredefinedSkillList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0x80)
        // Noise: A null lookup result is already reported by FindByPredefinedListID. Report only an existing object of the
        // wrong type. L2_LOG_ERROR(Log,
        //     L"[%s][%d] PredefinedSkillList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0x80)
        if (base != nullptr) {
            L2_LOG_ERROR(
                Log,
                "Cannot read predefined list %d as a skill list: its object has an incompatible type; no list is returned.",
                predefinedListId
            );
        }
        return nullptr;
    }
    if (skillList->GetPredefinedListType() != PREDEFINED_LIST_SKILL) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0x89,
        //     skillList->GetPredefinedListType())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0x89,
        //     skillList->GetPredefinedListType())
        L2_LOG_ERROR(
            Log, "Cannot read predefined skill list %d: unexpected list type code %d; no list is returned.",
            predefinedListId, static_cast<int>(skillList->GetPredefinedListType())
        );
        return nullptr;
    }
    return &skillList->m_vPredefinedSkillIDList;
}

std::vector<std::int64_t>* PredefinedListDB::GetPredefinedItemList(int predefinedListId) {
    BasePredefinedList* const base = PredefinedListDB::inst().FindByPredefinedListID(predefinedListId, true);
    auto* const itemList = dynamic_cast<PredefinedItemList*>(base);
    if (itemList == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PredefinedSkillList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0x97)
        // Noise: A null lookup result is already reported by FindByPredefinedListID. Report only an existing object of the
        // wrong type. L2_LOG_ERROR(Log,
        //     L"[%s][%d] PredefinedSkillList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0x97)
        if (base != nullptr) {
            L2_LOG_ERROR(
                Log,
                "Cannot read predefined list %d as an item list: its object has an incompatible type; no list is returned.",
                predefinedListId
            );
        }
        return nullptr;
    }
    if (itemList->GetPredefinedListType() != PREDEFINED_LIST_ITEM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0xa0,
        //     itemList->GetPredefinedListType())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0xa0,
        //     itemList->GetPredefinedListType())
        L2_LOG_ERROR(
            Log, "Cannot read predefined item list %d: unexpected list type code %d; no list is returned.", predefinedListId,
            static_cast<int>(itemList->GetPredefinedListType())
        );
        return nullptr;
    }
    return &itemList->m_vPredefinedItemIDList;
}

std::vector<TerritoryElement*>* PredefinedListDB::GetPredefinedTerritoryList(int predefinedListId) {
    BasePredefinedList* const base = PredefinedListDB::inst().FindByPredefinedListID(predefinedListId, true);
    auto* const territoryList = dynamic_cast<PredefinedTerritoryList*>(base);
    if (territoryList == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PredefinedTerritoryList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0xae)
        // Noise: A null lookup result is already reported by FindByPredefinedListID. Report only an existing object of the
        // wrong type. L2_LOG_ERROR(Log,
        //     L"[%s][%d] PredefinedTerritoryList* is nullptr",
        //     L"PredefinedListDB.cpp",
        //     0xae)
        if (base != nullptr) {
            L2_LOG_ERROR(
                Log,
                "Cannot read predefined list %d as a territory list: its object has an incompatible type; no list is "
                "returned.",
                predefinedListId
            );
        }
        return nullptr;
    }
    if (territoryList->GetPredefinedListType() != PREDEFINED_LIST_TERRITORY) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0xb7,
        //     territoryList->GetPredefinedListType())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid PredefinedListType [%d]",
        //     L"PredefinedListDB.cpp",
        //     0xb7,
        //     territoryList->GetPredefinedListType())
        L2_LOG_ERROR(
            Log, "Cannot read predefined territory list %d: unexpected list type code %d; no list is returned.",
            predefinedListId, static_cast<int>(territoryList->GetPredefinedListType())
        );
        return nullptr;
    }
    return &territoryList->m_vPredefinedTerritoryList;
}

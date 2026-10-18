#include "ObjectDB.h"
#include "Shared/src/MemorySub.h"

#include "Castle.h"
#include "Dominion.h"
#include "Item.h"
#include "NPC.h"
#include "ParserCombinationItem.h"
#include "ParserNPC.h"
#include "ParserObject.h"
#include "ParserSetItem.h"
#include "ParserShapeShiftedSetItem.h"
#include "ParserSoulshotEffect.h"
#include "ParserUpgradeSystem.h"
#include "Pet.h"
#include "Residence.h"
#include "RuneItem.h"
#include "Shared/src/SharedFactory.h"
#include "Skill.h"
#include "UseCountItemParser.h"
#include "User.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Log.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>


CObjectDB g_ObjectDB;
std::array<char, 40000> g_bRegisteredNpcID{};

L2_REGISTER_OBJECT_TYPE(CObjectDB)
L2_DEFINE_OBJECT_CLASS_NAME_STORAGE(CObjectDB)

CObjectDB::CObjectDB() :
    m_pLastParsedItem(nullptr),
    m_pLastParsedNpc(nullptr),
    m_nRegisteredItemCount(0),
    m_nRegisteredNPCCount(0),
    m_bOnlyItemPrecompiledHeader(false),
    m_item_pch_file(nullptr),
    m_item_pch_file2(nullptr),
    m_bOnlyNpcPrecompiledHeader(false),
    m_npc_pch_file(nullptr),
    m_ClassIndex(0) {
    g_bRegisteredNpcID.fill(0);
}

CObjectDB::~CObjectDB() {
    m_pLastParsedItem = nullptr;
    m_pLastParsedNpc = nullptr;
    m_item_pch_file = nullptr;
    m_item_pch_file2 = nullptr;
    m_npc_pch_file = nullptr;
}

L2_DEFINE_OBJECT_CLASS_ID(CObjectDB)

L2_DEFINE_OBJECT_CLASS_NAME(CObjectDB, m_pClassName_CObjectDB)

L2_DEFINE_OBJECT_ALLOCATOR(CObjectDB, __GetClassName())

L2_DEFINE_OBJECT_NEW(CObjectDB)

MemoryObject* CObjectDB::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    return MemoryObject::AddRef(callerFile, callerLine, type);
}

L2_DEFINE_OBJECT_RELEASE(CObjectDB)

CObject* CObjectDB::GetObjectW(std::int32_t classId, bool silent) const {
    L2_GUARD_FUNCTION();

    if (m_bOnlyItemPrecompiledHeader || m_bOnlyNpcPrecompiledHeader) {
        return nullptr;
    }

    const auto position = m_ClassIdToObject.find(classId);
    if (position != m_ClassIdToObject.end()) {
        return position->second;
    }

    if (!silent) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x205,
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x205,
        //     classId)
        L2_LOG_ERROR(Log, "No object prototype is registered for class ID %d; lookup failed.", classId);
        runtime::process::ReportCallStack(LOG_ERROR, false);
    }
    return nullptr;
}

CItem* CObjectDB::CreateItem(std::int32_t classId) const {
    L2_GUARD_FUNCTION();

    CObject* const object = GetObjectW(classId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateItem Failed. (GetObject Failed)",
        //     L"ObjectDB.cpp",
        //     0x560)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CreateItem Failed. (GetObject Failed)",
        //     L"ObjectDB.cpp",
        //     0x560)
        L2_LOG_ERROR(Log, "Cannot create item class %d: no object prototype is available.", classId);
        return nullptr;
    }

    {
        GuardHelper createItem1(L"createitem 1", CThreadManager::GetThreadID());
        if (!object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CObjectDB::CreateItem failed. "
            //     L"class_id[%d] is not a item.",
            //     L"ObjectDB.cpp",
            //     0x567,
            //     classId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CObjectDB::CreateItem failed. "
            //     L"class_id[%d] is not a item.",
            //     L"ObjectDB.cpp",
            //     0x567,
            //     classId)
            L2_LOG_ERROR(Log, "Cannot create item class %d: the registered prototype is not an item.", classId);
            return nullptr;
        }
    }

    CItem* item;
    {
        GuardHelper createItem2(L"createitem 2", CThreadManager::GetThreadID());
        item = static_cast<CItem*>(object->Copy());
    }

    {
        GuardHelper createItem3(L"createitem 3", CThreadManager::GetThreadID());
        const CItem* const prototype = static_cast<const CItem*>(object);

        std::int64_t amount = 0;
        int sourceLine = 0;
        switch (item->m_data->m_ItemConsumeType) {
            case ICT_NORMAL:
                amount = 1;
                sourceLine = 0x580;
                break;
            case ICT_CHARGE:
                amount = prototype->m_const->m_nInitialCount;
                sourceLine = 0x586;
                break;
            case ICT_STACKABLE:
                sourceLine = 0x58c;
                break;
            case ICT_ASSET:
                sourceLine = 0x592;
                break;
            default:
                break;
        }

        if (sourceLine != 0 && !item->SetAmount(amount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%d][%d]",
            //     "ObjectDB.cpp",
            //     sourceLine,
            //     item->m_data->m_nCount,
            //     amount)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] item amount is invalid[%d][%d]",
            //     "ObjectDB.cpp",
            //     sourceLine,
            //     item->m_data->m_nCount,
            //     amount)
            L2_LOG_ERROR(
                Log, "Item class %d rejected initial quantity %lld; stored quantity is %lld. Item creation continues.",
                classId, static_cast<long long>(amount), static_cast<long long>(item->m_data->m_nCount)
            );
        }
    }

    if (item->IsDuration()) {
        item->SetInitialDuration();
    }
    item->m_data->m_nDBID = -1;
    return item;
}

CNPC* CObjectDB::CreateNPC(std::int32_t classId, bool copy) const {
    // Current L2Server 0x008d729c.
    L2_GUARD_FUNCTION();

    // Noise: the fixed Gremlin class triggers a trace on every creation without testing for an error.
    // if (classId == 1000001) {
    //     // Log.Add(LOG_ERROR, L"Gremlin")
    //     L2_LOG_ERROR(Log, L"Gremlin")
    //     runtime::process::ReportCallStack(LOG_ERROR, false)
    // }

    CObject* const object = GetObjectW(classId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateNPC Failed. (GetObject Failed)",
        //     L"ObjectDB.cpp",
        //     0x5c1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CreateNPC Failed. (GetObject Failed)",
        //     L"ObjectDB.cpp",
        //     0x5c1)
        L2_LOG_ERROR(Log, "Cannot create NPC class %d: no object prototype is available.", classId);
        return nullptr;
    }
    if (!object->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CObjectDB::CreateNPC failed. "
        //     "class_id[%d] is not a NPC.",
        //     "ObjectDB.cpp",
        //     0x5c7,
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] CObjectDB::CreateNPC failed. "
        //     "class_id[%d] is not a NPC.",
        //     "ObjectDB.cpp",
        //     0x5c7,
        //     classId)
        L2_LOG_ERROR(Log, "Cannot create NPC class %d: the registered prototype is not an NPC.", classId);
        return nullptr;
    }

    CNPC* const prototype = object->CastNPC();
    CNPC* npc = nullptr;
    {
        LagAlert lagAlert(5000, "CObjectDB::CreateNPC", 0x5d0);
        npc = copy ? static_cast<CNPC*>(prototype->Copy()) : prototype->CopyFakeNPC();
    }

    npc->SetDefaultParameters();
    runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_Create);

    if (npc->m_data->m_nResidenceID < 1) {
        return npc;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(npc->m_data->m_nResidenceID);
    if (residence == nullptr) {
        return npc;
    }

    if (npc->m_data->m_bNotFunctionable) {
        npc->m_data->m_bNotFunctionable = false;
        {
            L2_WRITE_GUARD(*residence);
            residence->m_OwningNPCServerIdList.push_back(npc->Id());
        }
    }

    if (residence->IsCastle()) {
        CCastle* const castle = residence->CastCastle();
        if (castle != nullptr && castle->m_pDominion != nullptr && castle->m_pDominion->IsUnderDominionSiege()) {
            const int pledgeId = residence->m_data != nullptr ? static_cast<int>(residence->m_data->m_nResidenceOwnerID) : 0;
            residence->SetNPCPledge(npc, pledgeId);
        }
    }

    {
        L2_WRITE_GUARD(*residence);
        residence->m_AllOwningNPCServerIdList.push_back(npc->Id());
    }
    return npc;
}

CObject* CObjectDB::GetObjectByName(const char16_t* name) const {
    L2_GUARD_FUNCTION();

    if (m_bOnlyItemPrecompiledHeader || m_bOnlyNpcPrecompiledHeader) {
        return nullptr;
    }

    const auto position = m_NameToObject.find(name);
    if (position != m_NameToObject.end()) {
        return position->second;
    }

    // std::wstring hostName
    // if (name != nullptr) {
    //     for (const char16_t* current = name.
    //          *current != u'\0'.
    //          ++current) {
    //         hostName.push_back(
    //             static_cast<wchar_t>(*current))
    //     }
    // }
    const std::wstring hostName = Utf16ToWide(name);
    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] undefined (or unregistered) Name[%ls]",
    //     "ObjectDB.cpp",
    //     0x21e,
    //     hostName.c_str())
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] undefined (or unregistered) Name[%ls]",
    //     "ObjectDB.cpp",
    //     0x21e,
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log, u"No object prototype is registered for name '%ls'; lookup failed.",
        name == nullptr ? L"<null>" : hostName.c_str()
    );
    return nullptr;
}

const wchar_t* CObjectDB::GetAI(std::int32_t classId) const {
    L2_GUARD_FUNCTION();

    const auto position = m_ClassIdToAI.find(classId);
    if (position == m_ClassIdToAI.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x1c2,
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x1c2,
        //     classId)
        L2_LOG_ERROR(Log, "No AI is registered for class ID %d; using Default.", classId);
        return L"Default";
    }

#if defined(_WIN32)
    return reinterpret_cast<const wchar_t*>(position->second.c_str());
#else
    thread_local std::wstring hostAi;
    hostAi.clear();
    hostAi.reserve(position->second.size());
    for (const char16_t value : position->second) {
        hostAi.push_back(static_cast<wchar_t>(value));
    }
    return hostAi.c_str();
#endif
}

const char16_t* CObjectDB::GetName(std::int32_t classId) const {
    L2_GUARD_FUNCTION();

    const auto position = m_ClassIdToName.find(classId);
    if (position == m_ClassIdToName.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x1d2,
        //     classId)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
        //     "ObjectDB.cpp",
        //     0x1d2,
        //     classId)
        L2_LOG_ERROR(Log, "No name is registered for class ID %d; returning an empty name.", classId);
        return u"";
    }

    return position->second.c_str();
}

const wchar_t* CObjectDB::GetSysString(const std::uint8_t countryCode, const std::int32_t stringNumber) const {
    // Current L2Server 0x008d6930.
    L2_GUARD_FUNCTION();

    const std::CDualKey<std::uint8_t, std::int32_t> key(countryCode, stringNumber);
    const auto position = m_SysString.find(key);
    if (position != m_SysString.end()) {
        return position->second.c_str();
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] undefined (or unregistered) SysString "
    //     "countrycode:[%d] stringNo:[%d]",
    //     "ObjectDB.cpp",
    //     0x8c3,
    //     static_cast<int>(countryCode),
    //     stringNumber)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] undefined (or unregistered) SysString "
    //     "countrycode:[%d] stringNo:[%d]",
    //     "ObjectDB.cpp",
    //     0x8c3,
    //     static_cast<int>(countryCode),
    //     stringNumber)
    L2_LOG_ERROR(
        Log, "No system string is registered for country code %d, string ID %d; lookup failed.",
        static_cast<int>(countryCode), stringNumber
    );
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return nullptr;
}

void CObjectDB::SetSysString(const std::uint8_t countryCode, const std::int32_t stringNumber, const char16_t* const value) {
    // Current L2Server 0x008d9190. Existing keys are not
    // overwritten. wchar_t is UTF-16 in the target. Adapt only the LP64
    // host representation kept by the portable map.
    L2_GUARD_FUNCTION();

    const std::CDualKey<std::uint8_t, std::int32_t> key(countryCode, stringNumber);
    if (m_SysString.find(key) == m_SysString.end()) {
        m_SysString.emplace(key, target_utf16::ToHostCodeUnits(value));
    }
}

int CObjectDB::GetClassIdFromName(const char16_t* name, bool silent) const {
    L2_GUARD_FUNCTION();

    if (m_bOnlyItemPrecompiledHeader || m_bOnlyNpcPrecompiledHeader) {
        return -1;
    }

    const auto position = m_NameToClassId.find(name);
    if (position != m_NameToClassId.end()) {
        return position->second;
    }

    if (!silent) {
        // std::wstring hostName
        // if (name != nullptr) {
        //     for (const char16_t* current = name.
        //          *current != u'\0'.
        //          ++current) {
        //         hostName.push_back(
        //             static_cast<wchar_t>(*current))
        //     }
        // }
        const std::wstring hostName = Utf16ToWide(name);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] undefined (or unregistered) Name[%s]",
        //     "ObjectDB.cpp",
        //     0x1e9,
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] undefined (or unregistered) Name[%s]",
        //     "ObjectDB.cpp",
        //     0x1e9,
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, u"No class ID is registered for name '%ls'; lookup returns -1.",
            name == nullptr ? L"<null>" : hostName.c_str()
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
    }
    return -1;
}

bool CObjectDB::RegisterName(std::int32_t classId, wchar_t* name) {
    L2_GUARD_FUNCTION();

    if (m_bOnlyItemPrecompiledHeader && m_ClassIdToName.find(classId) != m_ClassIdToName.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] object id is duplicated[%d]",
        //     L"ObjectDB.cpp",
        //     0x233,
        //     classId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] object id is duplicated[%d]",
        //     L"ObjectDB.cpp",
        //     0x233,
        //     classId)
        L2_LOG_ERROR(
            Log, "Duplicate class ID %d while generating the item header; name registration was rejected.", classId
        );
        return false;
    }

    const std::u16string targetName = name == nullptr ? std::u16string{} : WideToUtf16(std::wstring_view(name));
    const std::u16string storedName{std::u16string_view(targetName)};
    m_ClassIdToName[classId] = storedName;
    m_NameToClassId[storedName] = classId;
    return true;
}

bool CObjectDB::RegisterName(CObject* object, wchar_t* name) { return RegisterName(object->m_nClassId, name); }

void CObjectDB::RegisterAI(CObject* object, wchar_t* name) {
    const std::u16string targetName = name == nullptr ? std::u16string{} : WideToUtf16(std::wstring_view(name));
    m_ClassIdToAI[object->m_nClassId].assign(std::u16string_view(targetName));
}

void CObjectDB::RegisterObject(CObject* object) {
    L2_GUARD_FUNCTION();

    const auto namePosition = m_ClassIdToName.find(object->m_nClassId);
    if (namePosition != m_ClassIdToName.end()) {
        const std::u16string name = namePosition->second;
        m_NameToObject[name] = object;
    }
    m_ClassIdToObject[object->m_nClassId] = object;

    const std::uint32_t classId = static_cast<std::uint32_t>(object->m_nClassId);
    if (classId < UINT32_C(1000000)) {
        ++m_nRegisteredItemCount;
        return;
    }

    ++m_nRegisteredNPCCount;
    CNPC* const npc = static_cast<CNPC*>(object);
    if (object->IsBoss()) {
        npc->Set(OBJECT_FIELD_NO_SLEEP_MODE, INT64_C(1));
    }
    npc->m_data->m_nClassIndex = m_ClassIndex;

    if (object->IsPet()) {
        CPet* const pet = object->CastPet();
        pet->InitObject();
        pet->CheckCategory();
    }
    if (object->IsSummon()) {
        CSummon* const summon = object->CastSummon();
        summon->CheckControlableCategory();
        summon->CheckMultiSummonPoint();
    }

    if (m_ClassIndex < 15000) {
        ++m_ClassIndex;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npc class count over (MAX=%d)",
        //     L"ObjectDB.cpp",
        //     0x2bd,
        //     15000)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] npc class count over (MAX=%d)",
        //     L"ObjectDB.cpp",
        //     0x2bd,
        //     15000)
        L2_LOG_ERROR(
            Log, "NPC class %d was registered at index %d, which has reached the limit %d; the index will not advance.",
            object->m_nClassId, m_ClassIndex, 15000
        );
    }

    if (static_cast<std::uint32_t>(CNPC::s_nMaxID) <= classId) {
        CNPC::s_nMaxID = object->m_nClassId;
    }

    const std::uint32_t npcIndex = classId - UINT32_C(1000000);
    if (npcIndex >= g_bRegisteredNpcID.size()) {
        throw std::out_of_range("invalid xarray<T> subscript");
    }
    g_bRegisteredNpcID[npcIndex] = 1;
}

void CObjectDB::PostLoad() {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"CObjectDB::PostLoad()")
    // L2_LOG_INFO(Log, L"CObjectDB::PostLoad()")
    L2_LOG_INFO(Log, "Finalizing object prototypes, item sets and capsule contents...");

    CRuneItem::InitRuneItemSkills();
    bool postLoadFailed = false;
    for (const auto& entry : m_ClassIdToObject) {
        CObject* const object = entry.second;
        if (!object->PostLoad() && !postLoadFailed) {
            postLoadFailed = true;
        }
        CRuneItem::AddRuneSelectSkill(dynamic_cast<CItem*>(object));
    }
    CRuneItem::PostLoad();
    g_ItemTempDataDB.clear();

    for (const auto& entry : m_SetItemIdToObject) {
        entry.second->PostLoad();
    }
    for (const auto& entry : m_NewSetItemIdToObject) {
        entry.second->PostLoad();
    }
    for (const auto& entry : m_ShapeShiftedSetItemIdToObject) {
        entry.second->PostLoad();
    }
    g_SetItemTempDataDB.clear();
    g_NewSetItemTempDataDB.clear();
    g_ShapeShiftedSetItemTempDataDB.clear();

    for (const auto& entry : g_CapsuledItemTempDataDB) {
        CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(entry.first);
        std::vector<TempCapsuledItem*>* const stagedItems = entry.second;
        for (TempCapsuledItem* const staged : *stagedItems) {
            const int classId = GetClassIdFromName(staged->m_ItemName.c_str(), false);
            if (classId == -1) {
                // const std::wstring itemName =
                //     target_utf16::ToHostCodeUnits(staged->m_ItemName.c_str())
                const std::wstring itemName = Utf16ToWide(staged->m_ItemName);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] undefined item[%s]",
                //     L"ObjectDB.cpp",
                //     0x534,
                //     itemName.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] undefined item[%s]",
                //     L"ObjectDB.cpp",
                //     0x534,
                //     itemName.c_str())
                L2_LOG_ERROR(
                    Log, u"Capsule item class %d refers to unknown item '%ls'; the entry is retained with class ID -1.",
                    entry.first, itemName.c_str()
                );
            }

            CapsuledItem* const item = new CapsuledItem{classId, staged->nMin, staged->nMax, staged->nProb};
            itemConstInfo->m_vCapsuledItemList.push_back(item);
            delete staged;
        }
        delete stagedItems;
    }
    g_CapsuledItemTempDataDB.clear();
    if (postLoadFailed) {
        L2_LOG_ERROR(Log, "One or more object post-load checks failed; the remaining post-load processing has completed.");
    }
}

bool CObjectDB::LoadItem(bool reload) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\Itemdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/Itemdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ObjectDB.cpp",
        //     0x440)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ObjectDB.cpp",
        //     0x440)
        L2_LOG_ERROR(Log, "Cannot read Itemdata.txt from the configured scripts directory; item loading failed.");
        return false;
    }

    return LoadItemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, reload);
}

bool CObjectDB::LoadItemData(char16_t* data, int dataLength, bool reload) {
    L2_GUARD_FUNCTION();

    CParserForObject parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyItemPrecompiledHeader;
    parser.m_bReload = reload;
    if (!parser.Create(this, data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x456)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x456)
        L2_LOG_ERROR(Log, "Cannot initialize the item parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing SetItem file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x462,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing SetItem file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x462,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse item data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }

    m_pLastParsedItem = parser.m_pItem;
    return true;
}

bool CObjectDB::LoadNPC(bool checkAI, bool reload) {
    // Current L2Server 0x008d83f0.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\npcdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/npcdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ObjectDB.cpp",
        //     0x4ba)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"ObjectDB.cpp",
        //     0x4ba)
        L2_LOG_ERROR(Log, "Cannot read npcdata.txt from the configured scripts directory; NPC loading failed.");
        return false;
    }

    return LoadNPCData(input.GetRawString() + 1, input.GetRawStringLen() - 1, checkAI, reload);
}

bool CObjectDB::LoadNPCData(char16_t* data, int dataLength, bool checkAI, bool reload) {
    // Current L2Server 0x008d44a4.
    L2_GUARD_FUNCTION();

    CParserForNPC parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyNpcPrecompiledHeader;
    parser.m_bReload = reload;
    if (!parser.Create(this, data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"ObjectDB.cpp",
        //     0x4cf)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"ObjectDB.cpp",
        //     0x4cf)
        L2_LOG_ERROR(Log, "Cannot initialize the NPC parser; loading failed.");
        return false;
    }

    if (checkAI) {
        parser.mCheckAI = true;
    }
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed line(%d)",
        //     L"ObjectDB.cpp",
        //     0x4df,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed line(%d)",
        //     L"ObjectDB.cpp",
        //     0x4df,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse NPC data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }

    m_pLastParsedNpc = parser.m_pNpc;
    return true;
}

bool CObjectDB::LoadCombinationItem(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\combinationitemdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/combinationitemdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from SetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x304)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from SetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x304)
        L2_LOG_ERROR(
            Log,
            "Cannot read combinationitemdata.txt from the configured scripts directory; combination-item loading failed."
        );
        return false;
    }

    return LoadCombinationItemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadCombinationItemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

    CParserForCombinationItem parser;
    if (!parser.Create(this, data, dataLength, onlyPrecompiledHeader)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x317)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x317)
        L2_LOG_ERROR(Log, "Cannot initialize the combination-item parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x323,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x323,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse combination-item data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

CombinationItemStruct* CObjectDB::FindCombinationItemStructBySlotOneItem(std::int32_t slotOneItem) {
    L2_GUARD_FUNCTION();

    for (auto& entry : m_mapCombinationItemStruct) {
        if (entry.first.m_PrimaryKey == slotOneItem) {
            return &entry.second;
        }
    }
    return nullptr;
}

CombinationItemStruct* CObjectDB::FindCombinationItemStructByMapKey(std::int32_t slotOneItem, std::int32_t slotTwoItem) {
    L2_GUARD_FUNCTION();

    const std::CDualKey<std::int32_t, std::int32_t> key(slotOneItem, slotTwoItem);
    const auto found = m_mapCombinationItemStruct.find(key);
    return found == m_mapCombinationItemStruct.end() ? nullptr : &found->second;
}

bool CObjectDB::IsExistCombination(std::int32_t slotOneItem, std::int32_t slotTwoItem) {
    L2_GUARD_FUNCTION();

    const std::CDualKey<std::int32_t, std::int32_t> key(slotOneItem, slotTwoItem);
    return m_mapCombinationItemStruct.find(key) != m_mapCombinationItemStruct.end();
}

void CObjectDB::RegisterCombinationItemStruct(CombinationItemStruct combinationItem) {
    L2_GUARD_FUNCTION();

    const std::CDualKey<std::int32_t, std::int32_t> key(combinationItem.SlotOne, combinationItem.SlotTwo);
    m_mapCombinationItemStruct.emplace(key, combinationItem);
}

void CObjectDB::SetCombinationItemStruct(CCombinationItem* combinationItem) {
    L2_GUARD_FUNCTION();

    const CombinationItemStruct value{combinationItem->m_nSlotOne, combinationItem->m_nSlotOneAmount,
        combinationItem->m_nSlotTwo, combinationItem->m_nSlotTwoAmount, combinationItem->m_nProb,
        combinationItem->m_nSuccessResultItem, combinationItem->m_nSuccessResultItemAmount,
        combinationItem->m_nFailResultItem, combinationItem->m_nFailResultItemAmount};
    RegisterCombinationItemStruct(value);
}

bool CObjectDB::LoadSoulShotEffect(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\soulshoteffectdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/soulshoteffectdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from SoulShotEffect file failed",
        //     L"ObjectDB.cpp",
        //     0x336)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from SoulShotEffect file failed",
        //     L"ObjectDB.cpp",
        //     0x336)
        L2_LOG_ERROR(
            Log, "Cannot read soulshoteffectdata.txt from the configured scripts directory; soulshot-effect loading failed."
        );
        return false;
    }

    return LoadSoulShotEffectData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadSoulShotEffectData(char16_t* data, int dataLength, bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

    CParserForSoulShotEffect parser;
    if (!parser.Create(this, data, dataLength, onlyPrecompiledHeader)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x349)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x349)
        L2_LOG_ERROR(Log, "Cannot initialize the soulshot-effect parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x355,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x355,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse soulshot-effect data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

void CObjectDB::SetRegisterSoulShotEffectObject(CSoulShoteffect* soulShotEffect) {
    L2_GUARD_FUNCTION();

    const SoulShotEffectStruct effect{soulShotEffect->nEffectType, soulShotEffect->nEffectLevel, soulShotEffect->nItemID,
        soulShotEffect->nSkillID, soulShotEffect->nSkillLevel};
    m_mapSoulShotEffect.emplace(soulShotEffect->nID, effect);
}

const CSkillInfo* CObjectDB::GetSoulShotSkill(const int action, CCreature* creature) {
    // Current L2Server 0x008d5e00.
    L2_GUARD_FUNCTION();

    if (creature == nullptr || (action != 3 && action != 4 && action != 5 && action != 0x12)) {
        return nullptr;
    }

    int bestEffectLevel = 0;
    int bestSkillId = 0;
    int bestSkillLevel = 0;
    const auto inspectSlot = [&](const int itemIndex, const SoulShotEffectStruct& effect) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (!item || item->GetClassId() != effect.ItemID) {
            return;
        }
        if (bestEffectLevel < effect.EffectLevel ||
            (bestEffectLevel == effect.EffectLevel && bestSkillLevel < effect.SkillLevel)) {
            bestEffectLevel = effect.EffectLevel;
            bestSkillId = effect.SkillID;
            bestSkillLevel = effect.SkillLevel;
        }
    };

    for (const auto& entry : m_mapSoulShotEffect) {
        const SoulShotEffectStruct& effect = entry.second;
        if (effect.EffectType != action) {
            continue;
        }
        for (const int itemIndex : creature->m_data->m_nSlotIndex) {
            inspectSlot(itemIndex, effect);
        }

        // The current loop also consumes the four-byte cell immediately
        // following m_nSlotIndex (offset +0x1d8).
        int trailingCell = 0;
        std::memcpy(&trailingCell, &creature->m_data->m_OriginalStat, sizeof(trailingCell));
        inspectSlot(trailingCell, effect);
    }

    if (bestSkillId == 0 || bestSkillLevel == 0) {
        return nullptr;
    }
    return g_SkillDB.GetSkillInfo(CSkillKey(bestSkillId, bestSkillLevel, 0), nullptr);
}

int CObjectDB::GetSoulShotClassID(CCreature* creature) const {
    // Current L2Server 0x008d6034.
    L2_GUARD_FUNCTION();

    if (creature == nullptr) {
        return 0;
    }

    if (User* const owner = creature->GetUserOrMaster()) {
        creature = owner->CastCreature();
        if (creature == nullptr) {
            return 0;
        }
    }

    int soulShotClassId = 0;
    for (const auto& entry : m_mapSoulShotEffect) {
        const SoulShotEffectStruct& effect = entry.second;
        if (effect.EffectType != 3 || effect.EffectLevel <= 0) {
            continue;
        }

        const auto inspectSlot = [&](const int itemIndex) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
            if (item && item->GetClassId() == effect.ItemID) {
                soulShotClassId = effect.ItemID;
            }
        };

        for (const int itemIndex : creature->m_data->m_nSlotIndex) {
            inspectSlot(itemIndex);
        }

        // The current loop runs from +0xdc through +0x1d8 inclusive. The
        // published slot array ends at +0x1d8, so the final iteration reads
        // the first four bytes of m_OriginalStat. Preserve that observable
        // target behavior without invoking an out-of-bounds C++ access.
        int trailingCell = 0;
        std::memcpy(&trailingCell, &creature->m_data->m_OriginalStat, sizeof(trailingCell));
        inspectSlot(trailingCell);
    }
    return soulShotClassId;
}

bool CObjectDB::LoadUseCountItem(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\UseCountItem.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/UseCountItem.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from UseCountItemData file failed",
        //     L"ObjectDB.cpp",
        //     0x369)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from UseCountItemData file failed",
        //     L"ObjectDB.cpp",
        //     0x369)
        L2_LOG_ERROR(
            Log, "Cannot read UseCountItem.txt from the configured scripts directory; use-count item loading failed."
        );
        return false;
    }

    return LoadUseCountItemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadUseCountItemData(char16_t* data, int dataLength, bool) {
    L2_GUARD_FUNCTION();

    CParserForUseCountItem parser;
    // Current 0x008d3cc0 accepts the public flag but calls the generated
    // three-argument Create body, so the flag has no parser-side effect.
    if (!parser.Create(data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x37b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x37b)
        L2_LOG_ERROR(Log, "Cannot initialize the use-count item parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing LoadUseCountItemData file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x386,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing LoadUseCountItemData file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x386,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse use-count item data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

bool CObjectDB::LoadSetItem(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\setitemdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/setitemdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from SetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x3da)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from SetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x3da)
        L2_LOG_ERROR(Log, "Cannot read setitemdata.txt from the configured scripts directory; item-set loading failed.");
        return false;
    }

    return LoadSetItemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadSetItemData(char16_t* data, int dataLength, bool) {
    L2_GUARD_FUNCTION();

    CParserForSetItem parser;
    // Current 0x008d3fd0 accepts the public flag but calls the generated
    // three-argument Create body. The lexer member therefore stays false.
    if (!parser.Create(this, data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x3ed)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x3ed)
        L2_LOG_ERROR(Log, "Cannot initialize the item-set parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x3f9,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x3f9,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse item-set data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

void CObjectDB::SetRegisterObject(CSetItem* setItem) {
    L2_GUARD_FUNCTION();

    const std::int32_t setItemId = static_cast<std::int32_t>(setItem->m_nSetItemId);
    const auto name = m_SetItemIdToName.find(setItemId);
    if (name != m_SetItemIdToName.end()) {
        m_SetNameToObject[name->second] = setItem;
    }

    m_SetItemIdToObject[setItemId] = setItem;
    m_SetKeyIdToObject[setItem->GetSlotSetItemId(ST_CHEST)] = setItem;
}

CSetItem* CObjectDB::GetSetItemBySetItemId(std::int32_t setItemId) const {
    // Current L2Server 0x008d64a0.
    L2_GUARD_FUNCTION();
    const auto position = m_SetItemIdToObject.find(setItemId);
    if (position != m_SetItemIdToObject.end()) {
        return position->second;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x7b3,
    //     setItemId)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x7b3,
    //     setItemId)
    L2_LOG_ERROR(Log, "No legacy item set is registered for set ID %d; lookup failed.", setItemId);
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return nullptr;
}

int CObjectDB::IsItSet(CCreature*, int* slotIndices, bool& additional2) const {
    // Current L2Server 0x008d65cc. The creature argument is kept by the
    // original signature but is not read by the current body.
    L2_GUARD_FUNCTION();
    additional2 = false;
    if (slotIndices == nullptr || slotIndices[ST_CHEST] == -1) {
        return 0;
    }

    CSPointer<CItem> chest = CItemFactory::Inst().FindObjectSP(slotIndices[ST_CHEST]);
    if (!chest) {
        return 0;
    }
    const auto setPosition = m_SetKeyIdToObject.find(chest->m_data->m_nClassID);
    if (setPosition == m_SetKeyIdToObject.end()) {
        return 0;
    }

    CSetItem* const setItem = setPosition->second;
    const int additionalEnchant = static_cast<int>(setItem->m_nAdditional2);
    if (additionalEnchant <= chest->m_data->GetEnchanted()) {
        additional2 = true;
    }

    for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
        if (setItem->GetSlotSetItemId(slot) <= 0) {
            continue;
        }
        CSPointer<CItem> equipped = CItemFactory::Inst().FindObjectSP(slotIndices[slot]);
        if (!equipped) {
            if (slot != static_cast<int>(setItem->m_nAdditionalItemSlot)) {
                additional2 = false;
                return 0;
            }
            continue;
        }

        const int classId = equipped->m_data->m_nClassID;
        if (setItem->SlotToSetItemClassID(slot, classId) != classId) {
            if (slot != static_cast<int>(setItem->m_nAdditionalItemSlot)) {
                additional2 = false;
                return 0;
            }
            continue;
        }
        if (additional2 && equipped->m_data->GetEnchanted() < additionalEnchant &&
            slot != static_cast<int>(setItem->m_nAdditionalItemSlot)) {
            additional2 = false;
        }
    }
    return static_cast<int>(setItem->m_nSetItemId);
}

CNewSetItem* CObjectDB::GetNewSetItemBySetItemId(std::int32_t setItemId) const {
    L2_GUARD_FUNCTION();

    if (setItemId <= 0) {
        return nullptr;
    }
    const auto position = m_NewSetItemIdToObject.find(setItemId);
    if (position != m_NewSetItemIdToObject.end()) {
        return position->second;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x71a,
    //     setItemId)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x71a,
    //     setItemId)
    L2_LOG_ERROR(Log, "No new item set is registered for set ID %d; lookup failed.", setItemId);
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return nullptr;
}

void CObjectDB::SetRegisterSetObject(CNewSetItem* setItem) {
    L2_GUARD_FUNCTION();

    const std::int32_t setItemId = static_cast<std::int32_t>(setItem->m_nNewSetItemId);
    const auto name = m_NewSetItemIdToName.find(setItemId);
    if (name != m_NewSetItemIdToName.end()) {
        m_NewSetNameToObject[name->second] = setItem;
    }
    m_NewSetItemIdToObject[setItemId] = setItem;
}

bool CObjectDB::LoadShapeShiftedSetItem(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\shapeshiftedsetitemdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/shapeshiftedsetitemdata.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from ShapeShiftedSetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x40e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from ShapeShiftedSetItem file failed",
        //     L"ObjectDB.cpp",
        //     0x40e)
        L2_LOG_ERROR(
            Log,
            "Cannot read shapeshiftedsetitemdata.txt from the configured scripts directory; shape-shifted item-set loading "
            "failed."
        );
        return false;
    }

    return LoadShapeShiftedSetItemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadShapeShiftedSetItemData(char16_t* data, int dataLength, bool) {
    L2_GUARD_FUNCTION();

    CParserForShapeShiftedSetItem parser;
    // Current 0x008d415c receives the public flag but calls the generated
    // three-argument Create body, leaving the lexer's PCH flag untouched.
    if (!parser.Create(this, data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x421)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x421)
        L2_LOG_ERROR(Log, "Cannot initialize the shape-shifted item-set parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x42d,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x42d,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse shape-shifted item-set data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

CShapeShiftedSetItem* CObjectDB::GetShapeShiftedSetItemBySetItemId(std::int32_t setItemId) const {
    L2_GUARD_FUNCTION();

    if (setItemId <= 0) {
        return nullptr;
    }
    const auto position = m_ShapeShiftedSetItemIdToObject.find(setItemId);
    if (position != m_ShapeShiftedSetItemIdToObject.end()) {
        return position->second;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x775,
    //     setItemId)
    // L2_LOG_ERROR(Log,
    //     "[%s][%d] undefined (or unregistered)ClassId[%d]",
    //     "ObjectDB.cpp",
    //     0x775,
    //     setItemId)
    L2_LOG_ERROR(Log, "No shape-shifted item set is registered for set ID %d; lookup failed.", setItemId);
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return nullptr;
}

void CObjectDB::SetRegisterSetObject(CShapeShiftedSetItem* setItem) {
    L2_GUARD_FUNCTION();

    m_ShapeShiftedSetItemIdToObject[static_cast<std::int32_t>(setItem->m_nShapeShiftedSetItemId)] = setItem;
}

bool CObjectDB::LoadUpgradeSystem(bool onlyPrecompiledHeader) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\upgradesystem.txt";
#else
    // Linux file names are case sensitive. The data file is UpgradeSystem.txt.
    constexpr const wchar_t* kFileName = L"../script/UpgradeSystem.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from UpgradeSystem file failed",
        //     L"ObjectDB.cpp",
        //     0x399)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from UpgradeSystem file failed",
        //     L"ObjectDB.cpp",
        //     0x399)
        L2_LOG_ERROR(
            Log, "Cannot read upgradesystem.txt from the configured scripts directory; item-upgrade loading failed."
        );
        return false;
    }

    return LoadUpgradeSystemData(input.GetRawString() + 1, input.GetRawStringLen() - 1, onlyPrecompiledHeader);
}

bool CObjectDB::LoadUpgradeSystemData(char16_t* data, int dataLength, bool) {
    L2_GUARD_FUNCTION();

    CParserForUpgradeSystem parser;
    // Current 0x008d3e44 does not forward the public loader's
    // onlyPrecompiledHeader argument into this generated parser/lexer pair.
    if (!parser.Create(this, data, dataLength)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x3ac)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"ObjectDB.cpp",
        //     0x3ac)
        L2_LOG_ERROR(Log, "Cannot initialize the item-upgrade parser; loading failed.");
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x3b8,
        //     parser.m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Objects file failed after line[%d]",
        //     L"ObjectDB.cpp",
        //     0x3b8,
        //     parser.m_lexer.line + 1)
        L2_LOG_ERROR(Log, "Cannot parse item-upgrade data at line %d; loading failed.", parser.m_lexer.line + 1);
        return false;
    }
    return true;
}

CUpgradeSystem* CObjectDB::CheckUpgradeSystem(const std::int32_t id) {
    // Current L2Server CObjectDB upgrade-map lookup consumed by
    // User::RequestUpgradeSystem.
    L2_GUARD_FUNCTION();
    const auto found = m_mapUpgradeSystem.find(id);
    return found == m_mapUpgradeSystem.end() ? nullptr : found->second;
}

CUpgradeSystem::CUpgradeSystem() {
    L2_GUARD_FUNCTION();
    m_nID = 0;
    m_nUpgradeID = 0;
    m_nUpgradeEnchant = 0;
    m_vMaterial.clear();
    m_nCommission = 0;
    m_nResultID = 0;
    m_nResultEnchant = 0;
}

CUpgradeSystem::~CUpgradeSystem() {
    L2_GUARD_FUNCTION();
    m_nID = 0;
    m_nUpgradeID = 0;
    m_nUpgradeEnchant = 0;
    m_vMaterial.clear();
    m_nCommission = 0;
    m_nResultID = 0;
    m_nResultEnchant = 0;
}

void CUpgradeSystem::SetID(std::int32_t id) {
    L2_GUARD_FUNCTION();
    m_nID = id;
}

void CUpgradeSystem::SetUpgradeItem(std::int32_t itemId, std::int32_t enchant) {
    L2_GUARD_FUNCTION();
    m_nUpgradeID = itemId;
    m_nUpgradeEnchant = enchant;
}

void CUpgradeSystem::SetMaterialItem(std::int32_t itemId, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    const UpgradeMaterial material(itemId, amount);
    m_vMaterial.push_back(material);
}

void CUpgradeSystem::SetCommission(std::int64_t commission) {
    L2_GUARD_FUNCTION();
    m_nCommission = commission;
}

void CUpgradeSystem::SetResultItem(std::int32_t itemId, std::int32_t enchant) {
    L2_GUARD_FUNCTION();
    m_nResultID = itemId;
    m_nResultEnchant = enchant;
}

CSoulShoteffect::CSoulShoteffect() {
    L2_GUARD_FUNCTION();
    nID = 0;
    nEffectType = 0;
    nEffectLevel = 0;
    nItemID = 0;
    nSkillID = 0;
    nSkillLevel = 0;
}

CSoulShoteffect::~CSoulShoteffect() {
    L2_GUARD_FUNCTION();
    nID = 0;
    nEffectType = 0;
    nEffectLevel = 0;
    nItemID = 0;
    nSkillID = 0;
    nSkillLevel = 0;
}

void CSoulShoteffect::Set(std::int32_t id) {
    L2_GUARD_FUNCTION();
    nID = id;
}

void CSoulShoteffect::SetType(std::int32_t effectType) {
    L2_GUARD_FUNCTION();
    nEffectType = effectType;
}

void CSoulShoteffect::SetLevel(std::int32_t effectLevel) {
    L2_GUARD_FUNCTION();
    nEffectLevel = effectLevel;
}

void CSoulShoteffect::SetItemID(std::int32_t itemId) {
    L2_GUARD_FUNCTION();
    nItemID = itemId;
}

void CSoulShoteffect::SetSkillID(std::int32_t skillId, std::int32_t skillLevel) {
    L2_GUARD_FUNCTION();
    nSkillID = skillId;
    nSkillLevel = skillLevel;
}

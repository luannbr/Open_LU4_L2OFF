#include "Shared/src/CategoryDataContent.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <charconv>
#include <cwchar>
#include <iterator>
#include <string>

namespace CategoryContent {
namespace {

} // namespace

ContentBase::ContentBase(int categoryContentType) : m_categoryContentType(categoryContentType) {}

int ContentBase::GetGroupId(int classType) {
    // Current body is present in the shared 2015 image. Current Cached.exe
    // keeps the same map/category traversal around 0x00402aa0.
    for (const auto& group : m_mapGroup) {
        const int categoryId = static_cast<int>(g_PrecompiledHeaderDB.Search(group.second));
        if (categoryId == -1) {
            const std::wstring groupName = Utf16ToWide(group.second);
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid group name [%s]",
            //     groupName.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[categories] Group \"%ls\" (group %d, content type %d) is absent from the precompiled headers. Cannot
            //     find a group for class %d.", groupName.c_str(), group.first, m_categoryContentType, classType)
            L2_LOG_ERROR(
                Log,
                u"[categories] Group \"%ls\" (group %d, content type %d) is absent from the precompiled headers; cannot "
                u"find a group for class %d.",
                groupName.c_str(), group.first, m_categoryContentType, classType
            );
            return -1;
        }
        if (CCategoryDataDB::Inst().IsInCategory(classType, categoryId)) {
            return group.first;
        }
    }
    return -1;
}

void ContentBase::GatherClassList(int groupId, std::vector<int>& classList) {
    // Current Cached.exe 0x00402e74.
    L2_GUARD_FUNCTION();

    const auto group = m_mapGroup.find(groupId);
    if (group == m_mapGroup.end()) {
        return;
    }

    const int categoryId = static_cast<int>(g_PrecompiledHeaderDB.Search(group->second));
    if (categoryId == -1) {
        const std::wstring groupName = Utf16ToWide(group->second);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid group name [%s]",
        //     groupName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Group \"%ls\" (group %d, content type %d) is absent from the precompiled headers. No classes
        //     appended.", groupName.c_str(), groupId, m_categoryContentType)
        L2_LOG_ERROR(
            Log,
            u"[categories] Group \"%ls\" (group %d, content type %d) is absent from the precompiled headers; no classes "
            u"appended.",
            groupName.c_str(), groupId, m_categoryContentType
        );
        return;
    }
    CCategoryDataDB::Inst().GatherItemsInCategory(categoryId, classList);
}

void ContentBase::CheckItemIntegrity() {
    // Current Cached.exe 0x00402fd8. Every configured group must resolve to
    // at least one class in categorydata.txt.
    L2_GUARD_FUNCTION();

    for (const auto& group : m_mapGroup) {
        std::vector<int> classes;
        GatherClassList(group.first, classes);
        if (!classes.empty()) {
            continue;
        }

        const std::wstring groupName = Utf16ToWide(group.second);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CategoryContent][%s] invalid item integrity",
        //     groupName.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Group \"%ls\" (group %d, content type %d) has no classes in categorydata.txt. Stopping the
        //     server after a script-integrity failure.", groupName.c_str(), group.first, m_categoryContentType)
        L2_LOG_ERROR(
            Log,
            u"[categories] Group \"%ls\" (group %d, content type %d) has no classes in categorydata.txt; stopping the "
            u"server after a script-integrity failure.",
            groupName.c_str(), group.first, m_categoryContentType
        );
        ForceCrash(CrashInfo::INVALID_SCRIPT_INTEGRITY);
        break;
    }
}

void ContentBase::SaveCategoryToDB() {
    // Current Cached.exe 0x004031ac. The active request-TLS database selector
    // is used for every group/item pair, and parameters are reset after each
    // execution on the reused DBConn.
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* statement = L"{Call lin_AddClassCategoryDB(?,?,?)}";
    DBConn connection(runtime::process::CurrentDatabaseId());

    for (const auto& group : m_mapGroup) {
        std::vector<int> classes;
        GatherClassList(group.first, classes);
        for (const int classId : classes) {
            const int categoryContentType = m_categoryContentType;
            const int groupId = group.first;
            connection.Prepare(statement);
            connection.BindParameter(&categoryContentType);
            connection.BindParameter(&groupId);
            connection.BindParameter(&classId);
            if (!connection.ExecutePrepared(statement)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]qsAddClassCategoryDB db fail "
                //     L"[%d][%d][%d]",
                //     kSourceFile,
                //     0xb8,
                //     categoryContentType,
                //     groupId,
                //     classId)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[categories] Cannot save class %d in group %d (content type %d) to the database. Stopping the server
                //     after a category-synchronization failure.", classId, groupId, categoryContentType)
                L2_LOG_ERROR(
                    Log,
                    u"[categories] Cannot save class %d in group %d (content type %d) to the database; stopping the server "
                    u"after a category-synchronization failure.",
                    classId, groupId, categoryContentType
                );
                ForceCrash(CrashInfo::CACHEDSCRIPT_IS_NOT_UPDATED);
            }
            connection.ResetParam();
        }
    }
}

ClassChange::ClassChange() : ContentBase(2) {
    // Current Cached.exe 0x00402b10. These spellings are the literal script
    // category keys present in the shipping image.
    m_mapGroup.insert({1, u"first_class_group"});
    m_mapGroup.insert({2, u"second_class_group"});
    m_mapGroup.insert({3, u"third_class_group"});
    m_mapGroup.insert({4, u"fourth_class_group"});
    m_mapGroup.insert({5, u"fifth_class_group"});
    m_mapGroup.insert({6, u"sixth_class_group"});
}

ClassRole::ClassRole() : ContentBase(0) {
    // Current Cached.exe 0x004036cc. "archor" is the shipping category key,
    // not a portable spelling correction.
    m_mapGroup.insert({ClassGroup::CRT_KNIGHT, u"class_role_knight_group"});
    m_mapGroup.insert({ClassGroup::CRT_WARRIOR, u"class_role_warrior_group"});
    m_mapGroup.insert({ClassGroup::CRT_ROGUE, u"class_role_rogue_group"});
    m_mapGroup.insert({ClassGroup::CRT_ARCHOR, u"class_role_archor_group"});
    m_mapGroup.insert({ClassGroup::CRT_WIZARD, u"class_role_wizard_group"});
    m_mapGroup.insert({ClassGroup::CRT_SUMMONER, u"class_role_summoner_group"});
    m_mapGroup.insert({ClassGroup::CRT_ENCHANTER, u"class_role_enchanter_group"});
    m_mapGroup.insert({ClassGroup::CRT_SUPPORTER, u"class_role_support_group"});
    m_mapGroup.insert({ClassGroup::CRT_NOVICE, u"class_role_novice_group"});
    GenerateClassString();
}

// Current Cached.exe 0x00403930 destroys m_mapClassStr, then the ContentBase
// category map, and finally the Singleton base. Default reverse member/base
// destruction is the exact source-level expression of that machine code.
ClassRole::~ClassRole() = default;

void ClassRole::GenerateClassString() {
    // Current Cached.exe 0x00403470 clears and regenerates all ten entries,
    // including the empty CRT_NONE entry at index zero.
    m_mapClassStr.clear();
    for (int role = ClassGroup::CRT_NONE; role < ClassGroup::CRT_MAX; ++role) {
        std::vector<int> classes;
        GatherClassList(role, classes);

        std::u16string classString;
        for (std::size_t index = 0; index < classes.size(); ++index) {
            char decimal[16]{};
            const auto converted = std::to_chars(decimal, decimal + sizeof(decimal), runtime::CheckedVectorAt(classes, index));
            for (const char* cursor = decimal; cursor != converted.ptr; ++cursor) {
                classString.push_back(static_cast<char16_t>(*cursor));
            }
            if (index + 1 < classes.size()) {
                classString.push_back(u',');
            }
        }
        m_mapClassStr.insert({role, std::u16string(std::u16string_view(classString))});
    }
}

const char16_t* ClassRole::FindClassString(int role) const noexcept {
    const auto found = m_mapClassStr.find(role);
    return found == m_mapClassStr.end() ? nullptr : found->second.c_str();
}

void CategoryContentInit() {
    // Current Cached.exe 0x00403a94.
    L2_GUARD_FUNCTION();
    ClassRole::inst().CheckItemIntegrity();
    ClassChange::inst().CheckItemIntegrity();
}

void SetupCategoryDB() {
    // Current Cached.exe 0x00403b3c deletes the previous category rows and
    // publishes only ClassRole. ClassChange is integrity-checked but is not
    // written by this startup path.
    L2_GUARD_FUNCTION();

    constexpr const wchar_t* statement = L"{Call lin_DeleteClassCategoryDB}";
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]delete class category db fail",
        //     kSourceFile,
        //     0xd1)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Cannot clear existing class-category database rows. Stopping the server after a
        //     category-synchronization failure.")
        L2_LOG_ERROR(
            Log,
            u"[categories] Cannot clear existing class-category database rows; stopping the server after a "
            u"category-synchronization failure."
        );
        ForceCrash(CrashInfo::CACHEDSCRIPT_IS_NOT_UPDATED);
        return;
    }
    ClassRole::inst().SaveCategoryToDB();
}

} // namespace CategoryContent

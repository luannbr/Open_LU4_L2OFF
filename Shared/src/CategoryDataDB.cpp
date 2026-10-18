#include "Shared/src/CategoryDataDB.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/ParserCategory.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Utils.h"

// Original source location used by the replaced diagnostics.
/*
namespace {

constexpr const wchar_t* kSourceFile =
    L"..\\Shared\\CategoryDataDB.cpp".

} // namespace
*/

// Current Cached.exe 0x004015fc, 115 bytes, exact SHA-256
// 519bec1972c08aeb5083b4100696cd876a42e409c28506bac966fc7d29ec46e4.
// The standard map starts empty. The remaining default member initializers
// publish the null FILE and false PCH-only flag.
CCategoryDataDB::CCategoryDataDB() = default;

// Current Cached.exe 0x00406a24, 91 bytes, exact SHA-256
// 1d75e180b07ad981588ec82830a9d4ffe18e35e9d49d041da53412e2efbf69cd.
CCategoryDataDB::~CCategoryDataDB() = default;

CCategoryDataDB& CCategoryDataDB::Inst() {
    // Current image guards and constructs the process object at 0x006bcb90
    // on first access. A function-local static provides the same lifetime.
    static CCategoryDataDB instance;
    return instance;
}

bool CCategoryDataDB::AddObjectInCategory(int categoryId, int classId) {
    L2_GUARD_FUNCTION();

    const auto found = m_Categories.find(categoryId);
    if (found == m_Categories.end()) {
        // Current 0x0041a1b8 allocates a 0x20-byte set object only on this
        // branch, inserts the class ID and publishes the raw pointer in
        // SetMap.
        CategorySet* const category = new CategorySet;
        category->insert(classId);
        m_Categories[categoryId] = category;
    } else {
        found->second->insert(classId);
    }
    return true;
}

bool CCategoryDataDB::IsInCategory(int classId, int categoryId) {
    L2_GUARD_FUNCTION();

    const auto category = m_Categories.find(categoryId);
    return category != m_Categories.end() && category->second->find(classId) != category->second->end();
}

void CCategoryDataDB::GatherItemsInCategory(int categoryId, std::vector<int>& items) {
    // Current Cached.exe 0x00406d90. The target appends the complete ordered
    // xset range and leaves an existing destination untouched.
    L2_GUARD_FUNCTION();

    const auto category = m_Categories.find(categoryId);
    if (category == m_Categories.end() || category->second == nullptr) {
        return;
    }
    for (const int item : *category->second) {
        items.push_back(item);
    }
}

void CCategoryDataDB::LoadCategoryData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kCategoryPath = L"..\\Script\\categorydata.txt";
#else
    constexpr const wchar_t* kCategoryPath = L"../Script/categorydata.txt";
#endif

    CSecureStream input(kCategoryPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     kSourceFile,
        //     0x43)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Cannot read categorydata.txt, category loading skipped.")
        L2_LOG_ERROR(Log, u"[categories] Cannot read categorydata.txt; category loading skipped.");
        return;
    }

    CParserForCategory parser;
    parser.m_bOnlyPrecompiledHeader = m_bOnlyPrecompiledHeader;
    if (!parser.Create(this, input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForCategory::create failed",
        //     kSourceFile,
        //     0x4d)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Cannot initialize the parser for categorydata.txt, category loading skipped.")
        L2_LOG_ERROR(Log, u"[categories] Cannot initialize the parser for categorydata.txt; category loading skipped.");
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CategoryData file failed "
        //     L"after line[%d]",
        //     kSourceFile,
        //     0x58,
        //     parser.m_lexer.getLine())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[categories] Parsing categorydata.txt failed after %d completed lines.",
        //     parser.m_lexer.getLine())
        L2_LOG_ERROR(
            Log, u"[categories] Parsing categorydata.txt failed after %d completed lines.", parser.m_lexer.getLine()
        );
    }
}

void CCategoryDataDB::Load() {
    // Current Cached.exe 0x0040719c, 183 bytes, SHA-256
    // 86383779d4c7029e9be5bf161d6655506388fe832eb2e14045ed1b607fb9a21f.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kScriptDirectory = L"..\\Script\\";
#else
    constexpr const wchar_t* kScriptDirectory = L"../Script/";
#endif

    // Current 0x0040719c always attempts both phases in this order.
    g_PrecompiledHeaderDB.Load(kScriptDirectory, nullptr);
    LoadCategoryData();
}

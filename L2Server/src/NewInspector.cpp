#include "NewInspector.h"

#include "CConfig.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <cstdlib>
#include <string>
#include <string_view>

CNewInspectorDB g_NewInspectorDB;

namespace {

std::wstring NewInspectorFileName() {
    return Utf16ToWide(std::u16string_view(g_newinspectorversion));
}

} // namespace

RGBA CNewInspectorDB::GetAnswer(const Query& query) const {
    L2_GUARD_FUNCTION();
    return m_qa[query.textureNo][query.x][query.y];
}

void CNewInspectorDB::Load() {
    // Current L2Server 0x0087d94c keeps the old class spelling in its
    // guard literal even though the complete physical owner is
    // CNewInspectorDB.
    static constexpr wchar_t __FUNC_NAME__[] = L"CInspectorDB::Load()";
    L2_GUARD_PROFILE(__FUNC_NAME__);
    GuardHelper __stack_var__(__FUNC_NAME__, CThreadManager::GetThreadID());

    const std::wstring fileName = NewInspectorFileName();
    int byteCount = 0;
    char* const image = LoadBinaryAbsolute(fileName.c_str(), byteCount, false);
    if (!image) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"loading serverdata.dat error.")
        // L2_LOG_ERROR(Log,
        //     L"loading serverdata.dat error.")
        L2_LOG_ERROR(Log, "Cannot read inspector data '%s'; loading stops.", WideToUtf8(fileName).c_str());
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Loading NewInspectorData[serverdata.dat]")
    // L2_LOG_INFO(Log,
    //     L"Loading NewInspectorData[serverdata.dat]")
    L2_LOG_INFO(Log, "Parsing inspector data '%s' (%d bytes).", WideToUtf8(fileName).c_str(), byteCount);

    auto* cursor = reinterpret_cast<unsigned char*>(image);
    auto* const begin = cursor;
    int texture = 0;
    int x = 0;
    int y = 0;
    if (byteCount != 4) {
        do {
            const unsigned a = *cursor++;
            const unsigned r = *cursor++;
            const unsigned g = *cursor++;
            const unsigned b = *cursor++;
            m_qa[texture][x][y] = RGBA{r, g, b, a};

            ++y;
            if (y == 64) {
                y = 0;
                ++x;
            }
            if (x == 128) {
                x = 0;
                ++texture;
            }
        } while (texture != 15 && cursor - begin != byteCount - 4);
    }

    int crc = 0;
    ReadIntMem(cursor, crc);
    static_assert(sizeof(m_crc) == sizeof(crc));
    m_crc = static_cast<unsigned int>(crc);
    std::free(image);
}

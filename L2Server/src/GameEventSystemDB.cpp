#include "GameEventSystem.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "CConfig.h"
#include "DB.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace {

constexpr char16_t kEventDataFilename[] = u"..\\Script\\eventdata.xml";

std::string EventDataHostPath(const char16_t* filename) {
    std::string path = filename == nullptr ? std::string() : ::Utf16ToUtf8(std::u16string_view(filename));
#if !defined(_WIN32)
    std::replace(path.begin(), path.end(), '\\', '/');
    constexpr std::string_view targetPrefix = "../Script/";
    if (path.rfind(targetPrefix, 0) == 0) {
        path.replace(3, 6, "script");
    }
#endif
    return runtime::ResolveResourcePath(path).string();
}

std::FILE* OpenEventDataFile(const char16_t* filename, const char* mode) {
#if defined(_WIN32)
    const std::wstring path = ::Utf8ToWide(EventDataHostPath(filename));
    const wchar_t* const wideMode = mode[0] == 'r' ? L"rb" : L"wb";
    std::FILE* stream = nullptr;
    ::_wfopen_s(&stream, path.c_str(), wideMode);
    return stream;
#else
    const std::string path = EventDataHostPath(filename);
    return std::fopen(path.c_str(), mode);
#endif
}

void CreateEventDataDirectory() {
#if defined(_WIN32)
    ::CreateDirectoryW(runtime::ResolveResourcePath(L"..\\Script\\EventData\\").wstring().c_str(), nullptr);
#else
    static_cast<void>(::mkdir(runtime::ResolveResourcePath("../script/EventData").string().c_str(), 0777));
#endif
}

} // namespace

void CEventManager::InitFromDB() {
    // Current L2Server 0x00764f8c, original owner GameEventSystem_db.cpp.
    L2_GUARD_FUNCTION();
    db.RequestLoadEventDataFromDB();
}

// void CEventManager::SaveEventDataFile(
//     const char16_t* filename,
//     const char16_t* contents) {
//     // Current L2Server 0x00765024, original owner GameEventSystem_db.cpp.
//     L2_GUARD_FUNCTION()
//     CreateEventDataDirectory()
//     std::FILE* const stream =
//         OpenEventDataFile(filename, "wb")
//     if (stream != nullptr) {
//         std::fputc(0xff, stream)
//         std::fputc(0xfe, stream)
//         std::fwrite(
//             contents,
//             sizeof(char16_t),
//             std::char_traits<char16_t>::length(contents),
//             stream)
//         std::fclose(stream)
//     }
// }
void CEventManager::SaveEventDataFile(const char16_t* filename, const char16_t* contents) {
    // Current L2Server 0x00765024, original owner GameEventSystem_db.cpp.
    L2_GUARD_FUNCTION();
    CreateEventDataDirectory();
    std::FILE* const stream = OpenEventDataFile(filename, "wb");
    if (stream != nullptr) {
        std::fputc(0xff, stream);
        std::fputc(0xfe, stream);
        std::fwrite(contents, sizeof(char16_t), std::char_traits<char16_t>::length(contents), stream);
        const int writeError = std::ferror(stream);
        const int closeResult = std::fclose(stream);
        if (writeError != 0 || closeResult != 0) {
            L2_LOG_ERROR(
                Log,
                "Failed to save event data file '%s' (write error: %s, close error: %s); initialization is still attempted.",
                EventDataHostPath(filename).c_str(), writeError != 0 ? "yes" : "no", closeResult != 0 ? "yes" : "no"
            );
        }
    } else {
        L2_LOG_ERROR(
            Log, "Cannot open event data file '%s' for writing; initialization is still attempted.",
            EventDataHostPath(filename).c_str()
        );
    }
}

void CEventManager::LoadCompleted(std::u16string filename) {
    // Current L2Server 0x00765184, original owner GameEventSystem_db.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    const std::u16string contents = JoinStringFromMap(m_mapContent);
    const std::u16string relativeFilename = GetRelativeEventData(filename);
    SaveEventDataFile(relativeFilename.c_str(), contents.c_str());
    Init(relativeFilename);
    ReloadAll();
    m_mapContent.clear();
}

void CEventManager::DumpEventDataToDB() {
    // Current L2Server 0x00765570, original owner GameEventSystem_db.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    std::array<char16_t, 0x8000> eventData{};
    std::FILE* const stream = OpenEventDataFile(kEventDataFilename, "rb");
    // The shipping owner assumes the production file exists and
    // consumes its two-byte UTF-16LE BOM without a null-stream check.
    std::fgetc(stream);
    std::fgetc(stream);
    std::fread(eventData.data(), sizeof(char16_t), eventData.size(), stream);
    std::fclose(stream);

    std::map<std::int32_t, std::u16string> chunks;
    SplitStringToMapFixedSize<0x1000>(chunks, eventData.data());
    for (const auto& entry : chunks) {
        db.RequestDumpEvent(
            entry.first, static_cast<int>(chunks.size()), std::u16string(u"eventdata_dump.xml"), entry.second
        );
    }
}

void CEventManager::ReplyLoadEventFromDB(int index, int total, std::u16string filename, std::u16string contents) {
    // Current L2Server 0x00765850, original owner GameEventSystem_db.cpp.
    L2_GUARD_FUNCTION();
    if (!g_bLoadEventDataFromDB) {
        return;
    }

    L2_GUARD(m_EventManagerLock);
    const int contentCount = static_cast<int>(m_mapContent.size());
    if (contentCount == 0) {
        // Log.Add(
        //     LOG_DEBUG,
        //     L"CEventManager::ReplyLoadEventFromDB is initializing. "
        //     L"mapsize:[%d]",
        //     contentCount)
        // Noise: An empty chunk map merely selects the incoming filename. This is not a completed event-data load and needs
        // no per-transfer trace. L2_LOG_DEBUG(Log,
        //     L"CEventManager::ReplyLoadEventFromDB is initializing. "
        //     L"mapsize:[%d]",
        //     contentCount)
        m_szFileName = filename;
    } else if (!(filename == m_szFileName)) {
        // const std::wstring hostFilename =
        //     ::Utf16ToWide(filename.view())
        const std::string hostFilename = ::Utf16ToUtf8(runtime::StringView(filename));
        // Log.Add(
        //     LOG_ERROR,
        //     L"CEventManager::ReplyLoadEventFromDB, request is "
        //     L"ignored[%s]",
        //     hostFilename.c_str())
        // L2_LOG_ERROR(Log,
        //     L"CEventManager::ReplyLoadEventFromDB, request is "
        //     L"ignored[%s]",
        //     hostFilename.c_str())
        L2_LOG_ERROR(
            Log,
            "Ignoring event-data chunk index %d (reported total %d) named '%s': assembling '%s' with %d chunks already "
            "stored.",
            index, total, hostFilename.c_str(), ::Utf16ToUtf8(runtime::StringView(m_szFileName)).c_str(), contentCount
        );
        return;
    }

    if (total == 0) {
        DumpEventDataToDB();
        return;
    }

    m_mapContent.emplace(index, contents);
    // The current comparison uses the map size captured before insertion.
    // Preserve that off-by-one-looking completion gate literally.
    if (contentCount == total) {
        LoadCompleted(filename);
    }
}

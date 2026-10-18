#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "CWatchList.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

constexpr WATCHTYPE kWatchNpc = static_cast<WATCHTYPE>(1);
constexpr WATCHTYPE kWatchItem = static_cast<WATCHTYPE>(2);

bool EqualsAsciiCaseInsensitive(const std::u16string& value, const char* expected) {
    std::size_t index = 0;
    for (; expected[index] != '\0'; ++index) {
        if (index >= value.size())
            return false;
        const char16_t character = value[index];
        if (character > 0x7f)
            return false;
        if (std::tolower(static_cast<unsigned char>(character)) !=
            std::tolower(static_cast<unsigned char>(expected[index]))) {
            return false;
        }
    }
    return index == value.size();
}

int ParseDecimal(const std::u16string& value) {
    std::size_t index = 0;
    while (index < value.size() && (value[index] == u' ' || value[index] == u'\t')) {
        ++index;
    }

    bool negative = false;
    if (index < value.size() && (value[index] == u'+' || value[index] == u'-')) {
        negative = value[index] == u'-';
        ++index;
    }

    std::uint32_t magnitude = 0;
    while (index < value.size() && value[index] >= u'0' && value[index] <= u'9') {
        magnitude = magnitude * 10u + static_cast<std::uint32_t>(value[index] - u'0');
        ++index;
    }
    const std::uint32_t bits = negative ? 0u - magnitude : magnitude;
    return static_cast<std::int32_t>(bits);
}

struct TargetWideFile {
    std::FILE* stream = nullptr;
    int openError = 0;
    std::uint32_t systemError = 0;
    bool eof = false;
    bool readFailureReported = false;

    char16_t Read() {
        std::array<unsigned char, 2> bytes{};
        // Original: if (std::fread(bytes.data(), 1, bytes.size(), stream) != bytes.size()) {
        const std::size_t bytesRead = std::fread(bytes.data(), 1, bytes.size(), stream);
        if (bytesRead != bytes.size()) {
            const runtime::logging::StorageErrorState preserveErrors;
            if (std::ferror(stream) && !readFailureReported) {
                readFailureReported = true;
                L2_LOG_ERROR(Log,
                    "[watch list] Reading watch_list.txt failed. Input is treated as ended; previously added entries remain and the parser still returns true.");
            }
            if (bytesRead == 1) {
                // Original: L2_LOG_WARN(Log,
                //     "[watch list] Incomplete UTF-16 code unit in watch_list.txt: read one byte instead of two. The reader returns its end-of-input marker, existing parsing behavior is retained.")
                L2_LOG_WARN(Log,
                    "[watch list] Incomplete UTF-16 character in watch_list.txt: one byte read instead of two; the incomplete character is discarded.");
            }
            // Win64 fgetwc returns WEOF, which truncates to 0xffff in the
            // target's 16-bit wchar_t local before feof is tested.
            eof = true;
            return static_cast<char16_t>(0xffff);
        }
        return static_cast<char16_t>(static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8));
    }
};

TargetWideFile OpenTargetWideFile() {
    TargetWideFile result;
#if defined(_WIN32)
    result.openError = ::_wfopen_s(&result.stream, runtime::ResolveResourcePath(L"..\\Script\\watch_list.txt").wstring().c_str(), L"rb");
    if (result.openError != 0) {
        result.systemError = ::GetLastError();
    }
#else
    errno = 0;
    result.stream = std::fopen(runtime::ResolveResourcePath("../Script/watch_list.txt").string().c_str(), "rb");
    if (result.stream == nullptr) {
        result.openError = errno;
        // POSIX has no independent Win32 last-error slot. Preserve both
        // diagnostic fields with the one native open error.
        result.systemError = static_cast<std::uint32_t>(errno);
    }
#endif
    return result;
}

} // namespace

CWatchList g_WatchList;

CWatchItem::CWatchItem(WATCHTYPE type, int objectId, const char16_t* information) {
    // Current watchlist.obj 0x0062f510 computes the length before validating
    // the type, and an unknown type returns without copying the description.
    std::size_t length = 0;
    while (information[length] != u'\0')
        ++length;

    if (type == kWatchNpc) {
        m_nNpcId = objectId;
    } else if (type == kWatchItem) {
        m_nItemId = objectId;
    } else {
        return;
    }

    const std::size_t copied = std::min<std::size_t>(length, 255);
    for (std::size_t index = 0; index < copied; ++index) {
        m_strInformation[index] = information[index];
    }
    m_strInformation[copied] = u'\0';
}

CWatchList::CWatchList()
    // Current Cached.exe 0x0062fcfc: construct the 4000-spin owner lock,
    // then the NPC and item std::xmap sentinels at +0x28 and +0x48.
    :
    m_lock(4000), m_npcs(), m_items() {}

CWatchList::~CWatchList() {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_lock);
    for (const auto& entry : m_npcs) {
        delete entry.second;
    }
    m_npcs.clear();
    for (const auto& entry : m_items) {
        delete entry.second;
    }
    m_items.clear();
}

CWatchItem* CWatchList::CheckWatchList(WATCHTYPE type, int objectId) const {
    L2_GUARD_FUNCTION();

    const WatchItemMap* items = nullptr;
    if (type == kWatchNpc) {
        items = &m_npcs;
    } else if (type == kWatchItem) {
        items = &m_items;
    } else {
        return nullptr;
    }

    const auto found = items->find(objectId);
    return found == items->end() ? nullptr : found->second;
}

bool CWatchList::ParseWatchList() {
    // Current watchlist.obj 0x0062f710.
    L2_GUARD_FUNCTION();

    TargetWideFile file = OpenTargetWideFile();
    if (file.openError != 0) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"CWatchList::ParseWatchList() file open failed. "
        //     L"errno(%d) GetLastError(%d)",
        //     file.openError, file.systemError
        // )
        // Original: L2_LOG_ERROR(Log,
        //     L"CWatchList::ParseWatchList() file open failed. "
        //     L"errno(%d) GetLastError(%d)", file.openError, file.systemError)
        // L2_LOG_ERROR(Log,
        //     "[watch list] Cannot open '%s' (CRT error %d, system error %u). Loading stopped. Existing entries remain unchanged.",
        //     runtime::ResolveResourcePath(L"..\\Script\\watch_list.txt").string().c_str(),
        //     file.openError, file.systemError)
#if defined(_WIN32)
        L2_LOG_ERROR(Log,
            "[watch list] Cannot open '%s' (CRT error %d, Win32 status %u). Loading stopped; existing entries remain unchanged.",
            runtime::ResolveResourcePath(L"..\\Script\\watch_list.txt").string().c_str(),
            file.openError, file.systemError);
#else
        // Noise: systemError duplicates errno on POSIX. Print the code once.
        L2_LOG_ERROR(Log,
            "[watch list] Cannot open '%s' (errno %d). Loading stopped; existing entries remain unchanged.",
            runtime::ResolveResourcePath("../Script/watch_list.txt").string().c_str(),
            file.openError);
#endif
        return false;
    }

    // The target ignores fseek's result, enters the lock while the stream is
    // still open, and consumes exactly one UTF-16 unit before its first feof
    // test. This also keeps its empty/BOM-only-file behavior.
    // std::fseek(file.stream, 0, SEEK_SET)
    if (std::fseek(file.stream, 0, SEEK_SET) != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        const int error = errno;
        L2_LOG_ERROR(Log,
            "[watch list] Cannot rewind watch_list.txt (CRT errno %d). Parsing continues from the current position.",
            error);
        errno = error;
    }
    L2_GUARD(m_lock);
    file.Read();
    WATCHTYPE type = kWatchNpc;
    while (!file.eof) {
        std::array<char16_t, 512> lineBuffer{};
        std::size_t written = 0;
        bool comment = false;
        char16_t previous = u'\0';
        for (int count = 0; count < 256; ++count) {
            char16_t character = file.Read();
            if (character == u'\n')
                break;
            if (comment)
                continue;
            if (character == u'\t' || character == u'\r') {
                character = u' ';
            }
            lineBuffer[written++] = character;
            if (character == u'/' && previous == u'/') {
                lineBuffer[written - 1] = u'\0';
                lineBuffer[written - 2] = u'\0';
                comment = true;
            }
            previous = character;
        }

        std::u16string line(lineBuffer.data());
        while (!line.empty() && line.back() == u' ') {
            line.pop_back();
        }
        if (EqualsAsciiCaseInsensitive(line, "npc")) {
            type = kWatchNpc;
            continue;
        }
        if (EqualsAsciiCaseInsensitive(line, "item")) {
            type = kWatchItem;
            continue;
        }

        int objectId = 0;
        std::u16string information;
        const std::size_t separator = line.find(u' ');
        if (separator != std::u16string::npos) {
            objectId = ParseDecimal(line.substr(0, separator));
            information = line.substr(separator + 1);
        }

        CWatchItem* watchItem = new CWatchItem(type, objectId, information.c_str());
        WatchItemMap& destination = type == kWatchNpc ? m_npcs : m_items;
        // Current std::map::insert drops a duplicate pair without deleting
        // the freshly allocated pointer. Preserve that shipping leak rather
        // than adding an unobserved cleanup branch.
        destination.emplace(objectId, watchItem);
    }
    // std::fclose(file.stream)
    if (std::fclose(file.stream) != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        const int error = errno;
        L2_LOG_ERROR(Log,
            "[watch list] Closing watch_list.txt failed (CRT errno %d). Loaded entries remain; the parser still returns true.",
            error);
        errno = error;
    }
    return true;
}

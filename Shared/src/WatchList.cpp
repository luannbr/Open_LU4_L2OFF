#include "Shared/src/WatchList.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Log.h"

#include <algorithm>
#include <array>
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

bool EqualsAsciiCaseInsensitive(const std::u16string& value, const char* expected) {
    std::size_t index = 0;
    while (expected[index] != '\0') {
        if (index >= value.size()) {
            return false;
        }
        char16_t left = value[index];
        unsigned char right = static_cast<unsigned char>(expected[index]);
        if (left >= u'A' && left <= u'Z') {
            left = static_cast<char16_t>(left - u'A' + u'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<unsigned char>(right - 'A' + 'a');
        }
        if (left != static_cast<char16_t>(right)) {
            return false;
        }
        ++index;
    }
    return index == value.size();
}

int ParseTargetDecimal(const std::u16string& value) noexcept {
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
        magnitude = magnitude * 10U + static_cast<std::uint32_t>(value[index] - u'0');
        ++index;
    }
    return static_cast<std::int32_t>(negative ? 0U - magnitude : magnitude);
}

struct TargetWideFile {
    std::FILE* stream = nullptr;
    int openError = 0;
    std::uint32_t systemError = 0;
    bool eof = false;

    char16_t Read() {
        std::array<unsigned char, 2> bytes{};
        // Original: if (std::fread(bytes.data(), 1, bytes.size(), stream) != bytes.size()) {
        const std::size_t bytesRead = std::fread(bytes.data(), 1, bytes.size(), stream);
        if (bytesRead != bytes.size()) {
            if (bytesRead == 1) {
                // Original: L2_LOG_WARN(Log,
                //     "[watch list] Incomplete UTF-16 code unit in watch_list.txt: read one byte instead of two. The reader
                //     returns its end-of-input marker, existing parsing behavior is retained.")
                L2_LOG_WARN(
                    Log,
                    "[watch list] Incomplete UTF-16 character in watch_list.txt: one byte read instead of two; the "
                    "incomplete character is discarded."
                );
            }
            eof = true;
            return static_cast<char16_t>(0xffff);
        }
        return static_cast<char16_t>(static_cast<std::uint16_t>(bytes[0]) | (static_cast<std::uint16_t>(bytes[1]) << 8));
    }
};

TargetWideFile OpenTargetWideFile() {
    TargetWideFile result{};
#if defined(_WIN32)
    result.openError =
        ::_wfopen_s(&result.stream, runtime::ResolveResourcePath(L"..\\Script\\watch_list.txt").wstring().c_str(), L"rb");
    if (result.openError != 0) {
        result.systemError = ::GetLastError();
    }
#else
    errno = 0;
    result.stream = std::fopen(runtime::ResolveResourcePath("../script/watch_list.txt").string().c_str(), "rb");
    if (result.stream == nullptr) {
        result.openError = errno;
        result.systemError = static_cast<std::uint32_t>(errno);
    }
#endif
    return result;
}

} // namespace

CWatchList g_WatchList;

CWatchItem::CWatchItem(WATCHTYPE type, int objectId, const char16_t* information) :
    m_nNpcId(0), m_nItemId(0), m_strInformation{} {
    std::size_t length = 0;
    while (information[length] != u'\0') {
        ++length;
    }
    if (type == WATCH_NPC) {
        m_nNpcId = objectId;
    } else if (type == WATCH_ITEM) {
        m_nItemId = objectId;
    } else {
        return;
    }
    const std::size_t copied = std::min<std::size_t>(length, 255);
    std::copy_n(information, copied, m_strInformation);
    m_strInformation[copied] = u'\0';
}

CWatchList::CWatchList() : m_lock(4000) {}

CWatchList::~CWatchList() {
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
    const WatchItemMap* const items = type == WATCH_NPC ? &m_npcs : type == WATCH_ITEM ? &m_items : nullptr;
    if (items == nullptr) {
        return nullptr;
    }
    const auto found = items->find(objectId);
    return found == items->end() ? nullptr : found->second;
}

bool CWatchList::ParseWatchList() {
    L2_GUARD_FUNCTION();
    TargetWideFile file = OpenTargetWideFile();
    if (file.openError != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CWatchList::ParseWatchList() file open failed. errno(%d) GetLastError(%d)",
        //     file.openError,
        //     file.systemError)
        const int diagnosticErrno = errno;
        try {
#if defined(_WIN32)
            const auto path =
                runtime::Utf8Bytes(runtime::ResolveResourcePath(L"..\\Script\\watch_list.txt").u8string());
            // Log.Add(LOG_ERROR,
            //     "[watch list] Cannot open \"%s\" (CRT error %d, Win32 error %u). Existing entries retained.",
            //     path.c_str(), file.openError, static_cast<unsigned>(file.systemError))
            L2_LOG_ERROR(
                Log, "[watch list] Cannot open \"%s\" (CRT error %d, Win32 error %u); existing entries retained.",
                path.c_str(), file.openError, static_cast<unsigned>(file.systemError)
            );
#else
            const auto path = runtime::Utf8Bytes(runtime::ResolveResourcePath("../script/watch_list.txt").u8string());
            // Log.Add(LOG_ERROR,
            //     "[watch list] Cannot open \"%s\" (errno %d). Existing entries retained.",
            //     path.c_str(), file.openError)
            L2_LOG_ERROR(
                Log, "[watch list] Cannot open \"%s\" (errno %d); existing entries retained.", path.c_str(), file.openError
            );
#endif
        } catch (...) {
            // Log.Add(LOG_ERROR,
            //     "[watch list] Cannot open watch_list.txt (open error %d, system error %u). Resolved path unavailable.
            //     Existing entries retained.", file.openError, static_cast<unsigned>(file.systemError))
            L2_LOG_ERROR(
                Log,
                "[watch list] Cannot open watch_list.txt (open error %d, system error %u); resolved path unavailable. "
                "Existing entries retained.",
                file.openError, static_cast<unsigned>(file.systemError)
            );
        }
        errno = diagnosticErrno;
        return false;
    }

    std::fseek(file.stream, 0, SEEK_SET);
    L2_GUARD(m_lock);
    file.Read();
    WATCHTYPE type = WATCH_NPC;
    while (!file.eof) {
        std::array<char16_t, 512> buffer{};
        std::size_t written = 0;
        bool comment = false;
        char16_t previous = u'\0';
        for (int count = 0; count < 256; ++count) {
            char16_t character = file.Read();
            if (character == u'\n') {
                break;
            }
            if (comment) {
                continue;
            }
            if (character == u'\t' || character == u'\r') {
                character = u' ';
            }
            buffer[written++] = character;
            if (character == u'/' && previous == u'/') {
                buffer[written - 1] = u'\0';
                buffer[written - 2] = u'\0';
                comment = true;
            }
            previous = character;
        }

        std::u16string line(buffer.data());
        while (!line.empty() && line.back() == u' ') {
            line.pop_back();
        }
        if (EqualsAsciiCaseInsensitive(line, "npc")) {
            type = WATCH_NPC;
            continue;
        }
        if (EqualsAsciiCaseInsensitive(line, "item")) {
            type = WATCH_ITEM;
            continue;
        }

        int objectId = 0;
        std::u16string information;
        const std::size_t separator = line.find(u' ');
        if (separator != std::u16string::npos) {
            objectId = ParseTargetDecimal(line.substr(0, separator));
            information = line.substr(separator + 1);
        }
        auto* const item = new CWatchItem(type, objectId, information.c_str());
        WatchItemMap& destination = type == WATCH_NPC ? m_npcs : m_items;
        destination.emplace(objectId, item);
    }
    if (std::ferror(file.stream)) {
        L2_LOG_ERROR(
            Log,
            "[watch list] Reading watch_list.txt ended with an I/O error. Entries already inserted remain in memory; the "
            "loader still returns success."
        );
    }
    std::fclose(file.stream);
    return true;
}

#include "EventDataHelper.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace {

void EscapeEventDataApostrophes(std::u16string& value) {
    // Current helper 0x004c8224 inserts a second apostrophe after each one.
    std::u16string text(runtime::StringView(value));
    std::size_t position = text.find(u'\'');
    while (position != std::u16string::npos) {
        text.insert(position + 1, 1, u'\'');
        position = text.find(u'\'', position + 2);
    }
    value.assign(text);
}

void UnescapeEventDataApostrophes(std::u16string& value) {
    // Current helper 0x004c851c repeatedly replaces "''" with "'".
    std::u16string text(runtime::StringView(value));
    std::size_t position = text.find(u"''");
    while (position != std::u16string::npos) {
        text.replace(position, 2, 1, u'\'');
        position = text.find(u"''");
    }
    value.assign(text);
}

void SplitEventData(const char16_t* contents, EventDataContents& result) {
    // Current helper 0x004c89b4 always emits floor(length/0x1000)+1
    // chunks, including an empty final chunk for exact multiples.
    if (contents == nullptr)
        return;
    const std::u16string_view source(contents);
    const int chunkCount = static_cast<int>(source.size() >> 12) + 1;
    for (int index = 0; index < chunkCount; ++index) {
        result.emplace(index, std::u16string(source.substr(static_cast<std::size_t>(index) * 0x1000, 0x1000)));
    }
}

void SendEventDataToServer(const std::u16string& eventName, EventDataContents& contents) {
    // Current helper 0x004c858c uses the process-wide active L2Server socket.
    CSocket* socket = g_pActiveServerSocket.load(std::memory_order_acquire);
    if (socket == nullptr)
        return;

    if (contents.empty()) {
        socket->Send("chddSS", 0xc6, 0x9d, 0, 0, u"", u"");
        return;
    }

    const std::u16string packetEventName = eventName;
    const int total = static_cast<int>(contents.size());
    for (auto& [index, chunk] : contents) {
        UnescapeEventDataApostrophes(chunk);
        socket->Send("chddSS", 0xc6, 0x9d, index, total, packetEventName.c_str(), chunk.c_str());
        // Noise: one message per packet fragment, with no delivery result.
        // Log.Add(LOG_ERROR, "[NO_ERROR]SendToServer [%d/%d]", index, total)
    }
}

} // namespace

CEventDataHelper::CEventDataHelper() = default;

CEventDataHelper::~CEventDataHelper() = default;

void CEventDataHelper::Initialize(const std::u16string& eventName, const std::u16string& comment) {
    // Current helper 0x004c84c4.
    m_startTime = runtime::crt::Time32Now();
    m_eventName = eventName;
    m_comment = comment;
}

void CEventDataHelper::Load() {
    // Current Cached.exe 0x004c8aa0.
    L2_GUARD_FUNCTION();
    L2_GUARD(*this);

    std::array<char16_t, 129> eventName{};
    std::array<char16_t, 32768> contents{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(eventName.data(), 0x102);
    connection.Bind(contents.data(), 0x10000);
    constexpr const wchar_t* statement = L"{Call lin_GetEventData}";
    connection.Prepare(statement);
    if (connection.ExecutePrepared(statement) && connection.Fetch()) {
        m_eventName = std::u16string(eventName.data());
        EventDataContents chunks;
        SplitEventData(contents.data(), chunks);
        SendEventDataToServer(m_eventName, chunks);
        return;
    }

    EventDataContents empty;
    SendEventDataToServer(m_eventName, empty);
}

bool CEventDataHelper::Save(int index, int total, std::u16string eventName, std::u16string comment, std::u16string contents) {
    // Current Cached.exe 0x004c8dc4.
    L2_GUARD_FUNCTION();
    L2_GUARD(*this);

    if (m_contents.empty()) {
        // Noise: routine initialization before the first event-data fragment. No persistence result yet.
        // Log.Add(
        //     LOG_NORMAL, "CEventDataHelper::Save is initializing. mapsize:[%d]", static_cast<int>(m_contents.size())
        // )
        Initialize(eventName, comment);
    } else {
        const std::int32_t now = runtime::crt::Time32Now();
        if (runtime::SubtractInt32Wrapping(
                now,
                m_startTime) > 60) {
            // Log.Add(
            //     LOG_NORMAL, "CEventDataHelper::Save is reinitializing. mapsize:[%d]",
            //     static_cast<int>(m_contents.size())
            // )
            L2_LOG_WARN(Log,
                "[event data] Discarding %zu buffered fragments: the upload session is over 60 seconds old. "
                "Starting a new session with incoming fragment %d of %d.",
                m_contents.size(), index, total);
            m_contents.clear();
            Initialize(eventName, comment);
        }

        if (!(eventName == m_eventName)) {
            const std::string printableName = Utf16ToUtf8(runtime::StringView(eventName));
            // Log.Add(LOG_ERROR, "CEventDataHelper::Save, request is ignored[%s]", printableName.c_str())
            L2_LOG_ERROR(Log,
                "[event data] Rejecting fragment %d of %d for event '%s': another event upload is active. "
                "The %zu buffered fragments are retained.",
                index, total, printableName.c_str(), m_contents.size());
            return false;
        }
    }

    const std::string printableName = Utf16ToUtf8(runtime::StringView(eventName));
    // Noise: per-fragment receipt is routine and does not confirm insertion or persistence.
    // Log.Add(
    //     LOG_NORMAL, "CEventDataHelper::Save [%d/%d], %s is received", index, total, printableName.c_str()
    // )
    m_contents.emplace(index, std::move(contents));

    if (static_cast<std::int64_t>(m_contents.size()) == static_cast<std::int64_t>(total)) {
        int expectedIndex = 0;
        for (const auto& [storedIndex, chunk] : m_contents) {
            if (storedIndex != expectedIndex) {
                // Log.Add(LOG_ERROR, "CEventDataHelper::Save, order is invalid")
                L2_LOG_ERROR(Log,
                    "[event data] Cannot save event '%s': expected fragment index %d, found %d among %zu buffered fragments. "
                    "Fragments are retained; database save was not attempted.",
                    printableName.c_str(), expectedIndex, storedIndex, m_contents.size());
                return false;
            }
            ++expectedIndex;
        }

        SaveComplete();
        // Noise: SaveComplete returns no result and clears fragments even on failure. This is not evidence of a successful save.
        // Log.Add(
        //     LOG_NORMAL, "CEventDataHelper::Save [%d/%d], %s is completed", index, total, printableName.c_str()
        // )
    }
    return true;
}

void CEventDataHelper::SaveComplete() {
    // Current Cached.exe 0x004c874c. Save calls this while already holding the
    // same recursive CIOCriticalSection, matching the target's nested Guard.
    L2_GUARD_FUNCTION();
    L2_GUARD(*this);

    std::u16string allContents;
    // Current Cached.exe unnamed body 0x005c9db8 constructs an empty
    // xwstring and appends every map value in key order. Keep it inline here:
    // the image supplies no independent owner/name for that body.
    for (const auto& [index, chunk] : m_contents) {
        allContents.append(chunk);
    }
    EscapeEventDataApostrophes(m_comment);
    EscapeEventDataApostrophes(allContents);

    // Bind game text directly. DBConn encodes UTF-8 at query execution.
    // These strings remain alive through ExecutePrepared.

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SaveEventData (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(m_eventName.c_str());
    connection.BindParameter(m_comment.c_str());
    connection.BindParameter(allContents.c_str());
    // if (connection.ExecutePrepared(statement) && g_pActiveServerSocket.load(std::memory_order_acquire) != nullptr) {
    const bool executed = connection.ExecutePrepared(statement);
    if (executed && g_pActiveServerSocket.load(std::memory_order_acquire) != nullptr) {
        SendEventDataToServer(m_eventName, m_contents);
        // Log.Add(LOG_NORMAL, "CEventDataHelper::SaveComplete is completed")
        L2_LOG_INFO(Log,
            "[event data] Database save executed for event '%s' (%zu fragments); forwarding to L2Server was attempted. Delivery is not confirmed.",
            Utf16ToUtf8(runtime::StringView(m_eventName)).c_str(), m_contents.size());
    } else if (!executed) {
        L2_LOG_ERROR(Log,
            "[event data] Database save failed for event '%s'; discarding %zu buffered fragments. No forwarding or retry is performed here.",
            Utf16ToUtf8(runtime::StringView(m_eventName)).c_str(), m_contents.size());
    } else {
        L2_LOG_WARN(Log,
            "[event data] Database save executed for event '%s', but no active L2Server socket was found. "
            "Discarding %zu buffered fragments without forwarding.",
            Utf16ToUtf8(runtime::StringView(m_eventName)).c_str(), m_contents.size());
    }

    // The current method clears only the chunk map, on both SQL success and
    // failure. The event name, comment and session timestamp remain intact.
    m_contents.clear();
}

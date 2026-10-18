#include "AnnounceTimer.h"

#include "CWorld.h"
#include "DB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {

// constexpr const char* kAnnounceTimerSource =
//     "AnnounceTimer.cpp".

std::array<std::int32_t, 10> IntervalAnnounce{
    10,
    20,
    30,
    40,
    50,
    60,
    120,
    240,
    360,
    1440,
};

std::size_t TargetUtf16Length(const char16_t* const message) noexcept {
    const char16_t* cursor = message;
    while (*cursor != u'\0') {
        ++cursor;
    }
    return static_cast<std::size_t>(cursor - message);
}

} // namespace

CAnnounceTimer::CAnnounceTimer() {}

CAnnounceTimer::~CAnnounceTimer() = default;

CAnnounceTimer* CAnnounceTimer::Inst() {
    // Current L2Server 0x00529f1c uses this exact function-local owner.
    static CAnnounceTimer instance;
    return &instance;
}

void CAnnounceTimer::ClearIntervalMsg() {
    // Current L2Server 0x0047aaa4 clears exactly 16000 bytes.
    std::memset(wszIntervalMsg, 0, sizeof(wszIntervalMsg));
}

void CAnnounceTimer::Init() {
    // Current L2Server 0x0047aab8.
    L2_GUARD_FUNCTION();
    bShowIntervalAnnounce = true;
    ClearIntervalMsg();
    AddTimer(1000, 1);
    db.LoadIntervalAnnounce();
}

void CAnnounceTimer::ShowIntervalMsg(const int intervalIndex) {
    // Current L2Server 0x0047ab88.
    L2_GUARD_FUNCTION();
    for (int messageIndex = 0; messageIndex < 10; ++messageIndex) {
        char16_t* const message = wszIntervalMsg[intervalIndex][messageIndex];
        const std::size_t length = TargetUtf16Length(message);
        if (length != 0) {
            std::wstring hostMessage = Utf16ToWide(std::u16string_view(message, length));
            CWorld::Inst().GodsVoice(hostMessage.data());
        }
    }
}

void CAnnounceTimer::SetIntervalMsg(const int interval, const int announceId, char16_t* const message) {
    // Current L2Server 0x0047ac64. The successful copy omits
    // the terminator and does not clear a previous longer message.
    L2_GUARD_FUNCTION();
    const std::uint32_t messageIndex = static_cast<std::uint32_t>(announceId) - 1U;
    if (messageIndex >= 10U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] interval msg id is not valid MAX(%d).[%d]",
        //     kAnnounceTimerSource,
        //     0x71,
        //     10,
        //     announceId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] interval msg id is not valid MAX(%d).[%d]",
        //     kAnnounceTimerSource,
        //     0x71,
        //     10,
        //     announceId)
        L2_LOG_ERROR(
            Log, "Cannot set interval announcement %d: expected message ID 1 through 10; existing messages are unchanged.",
            announceId
        );
        return;
    }

    for (int intervalIndex = 0; intervalIndex < 10; ++intervalIndex) {
        if (IntervalAnnounce[intervalIndex] != interval) {
            continue;
        }

        const std::size_t length = TargetUtf16Length(message);
        if (length < 0x51U) {
            std::memcpy(wszIntervalMsg[intervalIndex][messageIndex], message, length * sizeof(char16_t));
        } else {
            const std::string hostMessage = Utf16ToUtf8(std::u16string_view(message, length));
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Too long interval msg.[%s]",
            //     kAnnounceTimerSource,
            //     0x7b,
            //     hostMessage.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] Too long interval msg.[%s]",
            //     kAnnounceTimerSource,
            //     0x7b,
            //     hostMessage.c_str())
            L2_LOG_ERROR(
                Log,
                "Cannot set interval announcement %d for %d minutes: message has %zu UTF-16 units, maximum 80; previous "
                "message is retained. Text: '%s'",
                announceId, interval, static_cast<std::size_t>(length), hostMessage.c_str()
            );
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] Cannot find interval time.[%d]",
    //     kAnnounceTimerSource,
    //     0x83,
    //     interval)
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d] Cannot find interval time.[%d]",
    //     kAnnounceTimerSource,
    //     0x83,
    //     interval)
    L2_LOG_ERROR(
        Log,
        "Cannot set announcement %d: unsupported interval %d minutes; expected 10, 20, 30, 40, 50, 60, 120, 240, 360 or "
        "1440.",
        announceId, interval
    );
}

void CAnnounceTimer::IntervalAnnounceCheck() {
    // Current L2Server 0x0047ae10. Preserve the target's unusual duplicate
    // suppression: non-ten-minute values are checked on every timer tick.
    L2_GUARD_FUNCTION();
    if (!bShowIntervalAnnounce) {
        return;
    }

    const std::int32_t signedMinute = runtime::crt::Time32Now() / 60;
    const std::uint32_t minute = static_cast<std::uint32_t>(signedMinute);
    if (minute % 10U == 0U && m_nLastCheckedMin == minute) {
        return;
    }

    m_nLastCheckedMin = minute;
    for (int intervalIndex = 0; intervalIndex < 10; ++intervalIndex) {
        if (minute % static_cast<std::uint32_t>(IntervalAnnounce[intervalIndex]) == 0U) {
            ShowIntervalMsg(intervalIndex);
        }
    }
}

void CAnnounceTimer::TimerExpired(const int id) {
    // Current L2Server 0x0047af18.
    L2_GUARD_FUNCTION();
    if (id == 1) {
        IntervalAnnounceCheck();
        AddTimer(1000, 1);
    }
}

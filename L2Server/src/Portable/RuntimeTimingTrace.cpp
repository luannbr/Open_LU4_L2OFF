#include "Portable/RuntimeTimingTrace.h"

#include "L2Server/src/Object.h"

#include "Shared/src/Runtime/LogStorage.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/platform/Runtime.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

namespace runtime::runtime_timing {

namespace {

struct Subject final {
    std::uint32_t sid = 0;
    int classId = 0;
    char kind = '-';
};

struct TraceState final {
    bool enabled = false;
    int npcClassId = 0;
    std::uint32_t sid = 0;
    std::uint64_t maximumRecords = 12000;
    std::atomic<std::uint64_t> recordCount{0};
    std::atomic<bool> formatFailureReported{false};
    std::atomic<bool> truncationReported{false};
    std::mutex outputLock;
    std::FILE* output = nullptr;

    TraceState() {
        const logging::StorageErrorState preserveErrors;
        const char* const enabledValue = std::getenv("L2SERVER_TIMING_TRACE");
        if (enabledValue == nullptr || std::strcmp(enabledValue, "1") != 0) {
            return;
        }

        const auto parseUnsigned = [](const char* name, std::uint64_t fallback) {
            const char* const value = std::getenv(name);
            if (value == nullptr || *value == '\0') {
                return fallback;
            }
            errno = 0;
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(value, &end, 10);
            if (errno != 0 || end == value || *end != '\0') {
                char message[256]{};
                std::snprintf(
                    message, sizeof(message), "[timing trace] Invalid integer in %s; using fallback %llu.", name,
                    static_cast<unsigned long long>(fallback)
                );
                // logging::Write("l2server", logging::Level::Warning, message)
                logging::Write("l2server", logging::Level::Warning, message, __FILE__);
                return fallback;
            }
            return static_cast<std::uint64_t>(parsed);
        };

        const std::uint64_t classValue = parseUnsigned("L2SERVER_TIMING_TRACE_NPC_CLASS_ID", 0);
        const std::uint64_t sidValue = parseUnsigned("L2SERVER_TIMING_TRACE_SID", 0);
        maximumRecords = parseUnsigned("L2SERVER_TIMING_TRACE_MAX", maximumRecords);
        if (classValue > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
            sidValue > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) || maximumRecords == 0 ||
            (classValue == 0 && sidValue == 0)) {
            char message[512]{};
            std::snprintf(
                message, sizeof(message),
                "[timing trace] Tracing disabled: NPC class %llu, subject ID %llu, record limit %llu. "
                "Class must be within 0..2147483647, ID within 0..4294967295, "
                "at least one filter must be nonzero, and the limit must be positive.",
                static_cast<unsigned long long>(classValue), static_cast<unsigned long long>(sidValue),
                static_cast<unsigned long long>(maximumRecords)
            );
            // logging::Write("l2server", logging::Level::Warning, message)
            logging::Write("l2server", logging::Level::Warning, message, __FILE__);
            return;
        }

        npcClassId = static_cast<int>(classValue);
        sid = static_cast<std::uint32_t>(sidValue);
        const char* const outputPath = std::getenv("L2SERVER_TIMING_TRACE_FILE");
        if (outputPath == nullptr || *outputPath == '\0') {
            // logging::Write("l2server", logging::Level::Warning,
            //     "[timing trace] Tracing disabled: L2SERVER_TIMING_TRACE_FILE must name an output file.")
            logging::Write(
                "l2server", logging::Level::Warning,
                "[timing trace] Tracing disabled: L2SERVER_TIMING_TRACE_FILE must name an output file.", __FILE__
            );
            return;
        }
        errno = 0;
        output = std::fopen(outputPath, "w");
        if (output == nullptr) {
            logging::ReportLogStorageFailure("l2server", "open", "timing trace", errno != 0 ? errno : EIO, outputPath);
            return;
        }
        // std::fputs(
        //     "tick\tthread\tevent\tactor_sid\tactor_class\tactor_kind\t"
        //     "target_sid\ttarget_class\ttarget_kind\tdetails\n",
        //     output)
        // std::fflush(output)
        errno = 0;
        const int headerResult = std::fputs(
            "tick\tthread\tevent\tactor_sid\tactor_class\tactor_kind\t"
            "target_sid\ttarget_class\ttarget_kind\tdetails\n",
            output
        );
        const int headerError = errno;
        errno = 0;
        const int flushResult = std::fflush(output);
        const int flushError = errno;
        if (headerResult == EOF || flushResult != 0) {
            const int error = headerResult == EOF ? headerError : flushError;
            logging::ReportLogStorageFailure("l2server", "write", "timing trace", error != 0 ? error : EIO, outputPath);
        }
        enabled = true;
    }
};

TraceState& State() {
    // Process-lifetime state avoids shutdown-order coupling with recovered
    // globals and matches the lifetime of the isolated runtime directory.
    static TraceState* const state = new TraceState;
    return *state;
}

Subject Describe(CObject* object) noexcept {
    if (object == nullptr) {
        return {};
    }
    Subject result;
    result.sid = object->Id();
    result.classId = object->GetClassId();
    result.kind = object->IsPC() ? 'P' : object->IsNPC() ? 'N' : 'O';
    return result;
}

bool MatchesSubject(const TraceState& state, const Subject& subject) noexcept {
    // Retain player-side action stages so the selected NPC can be correlated
    // with the user request that caused them.
    if (subject.kind == 'P') {
        return true;
    }
    if (state.sid != 0 && subject.sid == state.sid) {
        return true;
    }
    return state.npcClassId != 0 && subject.kind == 'N' && subject.classId == state.npcClassId;
}

} // namespace

bool Matches(CObject* first, CObject* second) noexcept {
    const logging::StorageErrorState preserveErrors;
    const TraceState& state = State();
    if (!state.enabled) {
        return false;
    }
    return MatchesSubject(state, Describe(first)) || MatchesSubject(state, Describe(second));
}

void Record(const char* event, CObject* actor, CObject* target, const char* detailFormat, ...) {
    const logging::StorageErrorState preserveErrors;
    TraceState& state = State();
    const Subject actorSubject = Describe(actor);
    const Subject targetSubject = Describe(target);
    if (!state.enabled || (!MatchesSubject(state, actorSubject) && !MatchesSubject(state, targetSubject))) {
        return;
    }

    const std::uint64_t record = state.recordCount.fetch_add(1, std::memory_order_relaxed);
    if (record >= state.maximumRecords) {
        if (record == state.maximumRecords) {
            char message[192]{};
            std::snprintf(
                message, sizeof(message),
                "[timing trace] Record limit %llu reached; this event is skipped. Previously admitted writes may still "
                "finish.",
                static_cast<unsigned long long>(state.maximumRecords)
            );
            // logging::Write("l2server", logging::Level::Info, message)
            logging::Write("l2server", logging::Level::Info, message, __FILE__);
        }
        return;
    }

    char details[1536]{};
    va_list arguments;
    va_start(arguments, detailFormat);
    // std::vsnprintf(
    //     details,
    //     sizeof(details),
    //     detailFormat != nullptr ? detailFormat : "",
    //     arguments)
    const int detailLength =
        std::vsnprintf(details, sizeof(details), detailFormat != nullptr ? detailFormat : "", arguments);
    va_end(arguments);
    if (detailLength < 0 && !state.formatFailureReported.exchange(true, std::memory_order_relaxed)) {
        // logging::Write("l2server", logging::Level::Error,
        //     "[timing trace] Formatting event details failed. Affected TSV rows may be incomplete. Further format-failure
        //     notices are suppressed for this trace.")
        logging::Write(
            "l2server", logging::Level::Error,
            "[timing trace] Formatting event details failed; affected TSV rows may be incomplete. Further format-failure "
            "notices are suppressed for this trace.",
            __FILE__
        );
    } else if (detailLength >= static_cast<int>(sizeof(details)) &&
        !state.truncationReported.exchange(true, std::memory_order_relaxed)) {
        // logging::Write("l2server", logging::Level::Warning,
        //     "[timing trace] Event details exceeded 1535 bytes and were truncated. Further truncation notices are
        //     suppressed for this trace, TSV fields are unchanged.")
        logging::Write(
            "l2server", logging::Level::Warning,
            "[timing trace] Event details exceeded 1535 bytes and were truncated. Further truncation notices are suppressed "
            "for this trace; TSV fields are unchanged.",
            __FILE__
        );
    }

    const std::lock_guard<std::mutex> guard(state.outputLock);
    // std::fprintf(
    //     state.output,
    //     "%u\t%d\t%s\t%u\t%d\t%c\t%u\t%d\t%c\t%s\n",
    //     runtime::GetTickCount(),
    //     CThreadManager::GetThreadID(),
    //     event != nullptr ? event : "",
    //     actorSubject.sid,
    //     actorSubject.classId,
    //     actorSubject.kind,
    //     targetSubject.sid,
    //     targetSubject.classId,
    //     targetSubject.kind,
    //     details)
    // std::fflush(state.output)
    errno = 0;
    const int writeResult = std::fprintf(
        state.output, "%u\t%d\t%s\t%u\t%d\t%c\t%u\t%d\t%c\t%s\n", GetTickCount(), CThreadManager::GetThreadID(),
        event != nullptr ? event : "", actorSubject.sid, actorSubject.classId, actorSubject.kind, targetSubject.sid,
        targetSubject.classId, targetSubject.kind, details
    );
    const int writeError = errno;
    errno = 0;
    const int flushResult = std::fflush(state.output);
    const int flushError = errno;
    if (writeResult < 0 || flushResult != 0) {
        const int error = writeResult < 0 ? writeError : flushError;
        const auto path = logging::SnapshotLogFilePath(state.output);
        logging::ReportLogStorageFailure("l2server", "write", "timing trace", error != 0 ? error : EIO, path.data());
    } else {
        logging::ReportLogWriteSuccess("l2server", "timing trace", state.output);
    }
}

} // namespace runtime::runtime_timing

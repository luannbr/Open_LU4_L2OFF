#pragma once

#include "OperatorLog.h"
#include "Shared/src/TextRuntime.h"

#include <ostream>

namespace runtime::logging {

// Keep the caller's wide stream, character writes and exception policy.
// No CLog dependency, stream replacement, explicit flush, retry or error swallowing.
inline void WriteWideDiagnostic(std::wostream& destination, Level level, std::string_view message, const char* sourceFile) {
    // Noise: An empty/whitespace-only diagnostic carries no information.
    // Format returns an empty record for it. Do not mistake that
    // for a prefix failure and reintroduce a blank line through the fallback.
    // Use the same four whitespace bytes as Format, without changing nonempty
    // message contents or the caller's stream exception policy.
    if (message.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return;
    }
    Record record = Format(nullptr, level, message, sourceFile ? sourceFile : "");
    if (record.length == 0) {
        // Preserve the error body if the prefix cannot be produced. Keep this
        // fallback bounded and escape controls just like the normal transport.
        constexpr std::string_view marker = " [truncated]";
        std::size_t used = 0;
        const auto consumed = AppendEscapedLogText(record, used, message, record.bytes.size() - marker.size() - 1);
        if (consumed != message.size()) {
            for (char byte : marker) {
                record.bytes[used++] = byte;
            }
        }
        record.bytes[used++] = '\n';
        record.length = used;
    }
    const std::wstring output = Utf8ToWide(std::string_view(record.bytes.data(), record.length));
    for (wchar_t value : output) {
        destination.put(value);
    }
}

} // namespace runtime::logging

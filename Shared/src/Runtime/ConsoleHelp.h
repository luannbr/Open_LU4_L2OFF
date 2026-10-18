#pragma once

// Operator console replies, such as the help list. They are plain text for the
// person at the console, not log records, so they have no time or level prefix.

#include "OperatorLog.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace runtime {

struct ConsoleCommandHelp {
    std::string_view usage;
    std::string_view description;
};

namespace console_help_detail {

inline constexpr std::size_t LineWidth = 100;
inline constexpr std::size_t MaxUsageColumn = 44;

// Append words of text at the given indent. Long words are not split.
inline void AppendWrapped(std::string& out, std::string_view text, std::size_t firstIndent, std::size_t indent) {
    std::size_t column = firstIndent;
    bool lineEmpty = true;
    while (!text.empty()) {
        const auto start = text.find_first_not_of(' ');
        if (start == std::string_view::npos) break;
        text.remove_prefix(start);
        const auto end = std::min(text.find(' '), text.size());
        const std::string_view word = text.substr(0, end);
        text.remove_prefix(end);
        if (!lineEmpty && column + 1 + word.size() > LineWidth) {
            out += '\n';
            out.append(indent, ' ');
            column = indent;
            lineEmpty = true;
        }
        if (!lineEmpty) {
            out += ' ';
            ++column;
        }
        out += word;
        column += word.size();
        lineEmpty = false;
    }
    out += '\n';
}

} // namespace console_help_detail

// Builds an aligned command list. Notes follow it as wrapped paragraphs.
inline std::string FormatConsoleHelp(
    std::string_view title, std::span<const ConsoleCommandHelp> commands, std::span<const std::string_view> notes = {}
) {
    using namespace console_help_detail;
    std::size_t usageWidth = 0;
    for (const auto& command : commands) {
        if (command.usage.size() <= MaxUsageColumn) usageWidth = std::max(usageWidth, command.usage.size());
    }
    const std::size_t descriptionColumn = 2 + usageWidth + 2;

    std::string out;
    out += '\n';
    out += title;
    out += '\n';
    for (const auto& command : commands) {
        out += "  ";
        out += command.usage;
        if (command.description.empty()) {
            out += '\n';
            continue;
        }
        if (command.usage.size() > usageWidth) {
            // A very long usage gets its own line. The description starts below it.
            out += '\n';
            out.append(descriptionColumn, ' ');
        } else {
            out.append(usageWidth - command.usage.size() + 2, ' ');
        }
        AppendWrapped(out, command.description, descriptionColumn, descriptionColumn);
    }
    for (const auto note : notes) {
        out += '\n';
        AppendWrapped(out, note, 0, 0);
    }
    out += '\n';
    return out;
}

// Writes text to stdout in whole records, so log records from other threads do
// not split a short reply. Uses the log console path, so a closed console pipe
// does not raise SIGPIPE and failed text goes to stderr.
inline bool WriteConsoleText(std::string_view text) noexcept {
    bool written = true;
    while (!text.empty()) {
        logging::Record record;
        record.length = std::min(text.size(), record.bytes.size());
        std::memcpy(record.bytes.data(), text.data(), record.length);
        text.remove_prefix(record.length);
        written = logging::WriteConsoleRecord(record) && written;
    }
    return written;
}

} // namespace runtime

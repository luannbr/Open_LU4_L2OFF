#pragma once

#include <string_view>
#include <utility>

namespace runtime::logging {

inline std::string_view SourceFileName(std::string_view source) noexcept {
    const auto separator = source.find_last_of("/\\");
    return separator == std::string_view::npos ? source : source.substr(separator + 1);
}

// Call arguments are evaluated before LogAt enters this scope. A logger consumes
// the source at entry, so diagnostics from formatting/storage do not inherit it.
// This portable metadata does not change recovered logger layouts or messages.
class SourceFileScope final {
public:
    explicit SourceFileScope(const char* source) noexcept : previous_(Pending()) { Pending() = source ? source : ""; }
    ~SourceFileScope() { Pending() = previous_; }
    SourceFileScope(const SourceFileScope&) = delete;
    SourceFileScope& operator=(const SourceFileScope&) = delete;

    static const char* Consume() noexcept {
        const char* source = Pending();
        Pending() = "";
        return source;
    }

private:
    static const char*& Pending() noexcept {
        static thread_local const char* source = "";
        return source;
    }
    const char* previous_;
};

} // namespace runtime::logging

// Computed severity is evaluated once, together with the other call arguments.
#define L2_LOG(logger, level, ...) ::runtime::logging::LogAt((logger), (level), __FILE__, __VA_ARGS__)

// Each product's logger header supplies its typed LogAt adapter. Passing the
// logger keeps destination selection visible at the call site.
#define L2_LOG_ERROR(logger, ...) ::runtime::logging::LogAt((logger), LOG_ERROR, __FILE__, __VA_ARGS__)
#define L2_LOG_WARN(logger, ...) ::runtime::logging::LogAt((logger), LOG_WARN, __FILE__, __VA_ARGS__)
#define L2_LOG_INFO(logger, ...) ::runtime::logging::LogAt((logger), LOG_NORMAL, __FILE__, __VA_ARGS__)
#define L2_LOG_DEBUG(logger, ...) ::runtime::logging::LogAt((logger), LOG_DEBUG, __FILE__, __VA_ARGS__)

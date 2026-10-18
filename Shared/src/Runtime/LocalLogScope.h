#pragma once

namespace runtime::log {

// Host logging policy, separate from recovered CLog/socket object layouts.
// Diagnostics about LogD must not use that transport or request a connection
// while reporting its failure. Preserve severity, console options and files.
class LocalLogScope final {
public:
    LocalLogScope() noexcept : previous_(State()) { State() = true; }
    ~LocalLogScope() { State() = previous_; }
    LocalLogScope(const LocalLogScope&) = delete;
    LocalLogScope& operator=(const LocalLogScope&) = delete;

    static bool IsActive() noexcept { return State(); }

private:
    static bool& State() noexcept {
        static thread_local bool active = false;
        return active;
    }
    bool previous_;
};

} // namespace runtime::log

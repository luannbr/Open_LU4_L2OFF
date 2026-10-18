#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace runtime {

// Use one reader per input handle, from the process loop. The handle is borrowed.
class ConsoleInput {
public:
    using Handler = std::function<void(std::string_view)>;
    static constexpr std::size_t MaxLineBytes = 1024;
    static constexpr std::size_t MaxPollWork = 4096;

#ifdef _WIN32
    ConsoleInput();
#else
    explicit ConsoleInput(int descriptor = 0);
#endif
    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;

    // Never wait for a complete line. Views are valid only during the callback.
    // EOF delivers a final partial line and disables input without stopping the process.
    // Errors contain no input text. An oversized line is discarded in full.
    void Poll(const Handler& onLine, const Handler& onError = {});
    bool IsOpen() const noexcept { return open_; }

private:
    void Consume(char byte, const Handler& onLine, const Handler& onError);
    void EndInput(const Handler& onLine);
    void FailInput(const Handler& onError);

#ifdef _WIN32
    void ConsumeConsoleCharacter(char16_t character, const Handler& onLine, const Handler& onError);
    void* handle_ = nullptr;
    char16_t highSurrogate_ = 0;
    char16_t repeatedCharacter_ = 0;
    std::uint16_t repeatCount_ = 0;
#else
    int descriptor_;
#endif
    std::string line_;
    bool open_ = true;
    bool discarding_ = false;
    bool skipLineFeed_ = false;
};

} // namespace runtime

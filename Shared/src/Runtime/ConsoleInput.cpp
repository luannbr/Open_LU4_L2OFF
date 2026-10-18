#include "ConsoleInput.h"

#include <algorithm>
#include <array>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Shared/src/TextRuntime.h"
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace runtime {

#ifdef _WIN32
ConsoleInput::ConsoleInput() : handle_(::GetStdHandle(STD_INPUT_HANDLE)) {}
#else
ConsoleInput::ConsoleInput(int descriptor) : descriptor_(descriptor) {}
#endif

void ConsoleInput::Consume(char byte, const Handler& onLine, const Handler& onError) {
    if (skipLineFeed_) {
        skipLineFeed_ = false;
        if (byte == '\n') return;
    }
    if (byte == '\r' || byte == '\n') {
        skipLineFeed_ = byte == '\r';
        const bool deliver = !discarding_;
        discarding_ = false;
        std::string completed = std::move(line_);
        line_.clear();
        if (deliver && onLine) onLine(completed);
        return;
    }
    if (discarding_) return;
    if (line_.size() == MaxLineBytes) {
        line_.clear();
        discarding_ = true;
        if (onError) onError("Console command exceeds 1024 bytes and was discarded.");
        return;
    }
    line_.push_back(byte);
}

void ConsoleInput::EndInput(const Handler& onLine) {
    open_ = false;
    std::string completed = std::move(line_);
    line_.clear();
    if (!discarding_ && !completed.empty() && onLine) onLine(completed);
}

void ConsoleInput::FailInput(const Handler& onError) {
    open_ = false;
    line_.clear();
    if (onError) onError("Reading console input failed. Command input is disabled.");
}

#ifdef _WIN32
void ConsoleInput::ConsumeConsoleCharacter(char16_t character, const Handler& onLine, const Handler& onError) {
    if (character == u'\b') {
        if (highSurrogate_ != 0) {
            highSurrogate_ = 0;
        } else if (!line_.empty() && !discarding_) {
            std::size_t start = line_.size() - 1;
            while (start > 0 && (static_cast<unsigned char>(line_[start]) & 0xc0) == 0x80) --start;
            line_.resize(start);
        }
        return;
    }
    if (highSurrogate_ != 0) {
        const char16_t high = std::exchange(highSurrogate_, 0);
        if (character >= 0xdc00 && character <= 0xdfff) {
            const char16_t pair[]{high, character};
            for (const char byte : Utf16ToUtf8(std::u16string_view(pair, 2))) Consume(byte, onLine, onError);
            return;
        }
        for (const char byte : Utf16ToUtf8(std::u16string_view(&high, 1))) Consume(byte, onLine, onError);
    }
    if (character == 0x1a) {
        EndInput(onLine);
    } else if (character >= 0xd800 && character <= 0xdbff) {
        highSurrogate_ = character;
    } else {
        for (const char byte : Utf16ToUtf8(std::u16string_view(&character, 1))) Consume(byte, onLine, onError);
    }
}
#endif

void ConsoleInput::Poll(const Handler& onLine, const Handler& onError) {
    if (!open_) return;
    std::size_t remaining = MaxPollWork;
    std::array<char, MaxPollWork> bytes{};
#ifdef _WIN32
    if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
        FailInput(onError);
        return;
    }
    DWORD consoleMode = 0;
    if (::GetConsoleMode(handle_, &consoleMode)) {
        // Read input events so an unfinished command cannot block shutdown.
        // ReadConsoleInput does not echo characters or change the console mode.
        while (open_ && remaining > 0) {
            --remaining;
            if (repeatCount_ != 0) {
                --repeatCount_;
                ConsumeConsoleCharacter(repeatedCharacter_, onLine, onError);
                continue;
            }
            DWORD available = 0;
            if (!::GetNumberOfConsoleInputEvents(handle_, &available)) {
                FailInput(onError);
                return;
            }
            if (available == 0) return;
            INPUT_RECORD event{};
            DWORD count = 0;
            if (!::ReadConsoleInputW(handle_, &event, 1, &count)) {
                FailInput(onError);
                return;
            }
            if (count != 0 && event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown &&
                event.Event.KeyEvent.uChar.UnicodeChar != 0) {
                repeatedCharacter_ = static_cast<char16_t>(event.Event.KeyEvent.uChar.UnicodeChar);
                repeatCount_ = std::max<WORD>(1, event.Event.KeyEvent.wRepeatCount);
            }
        }
        return;
    }
    const DWORD type = ::GetFileType(handle_);
    if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) {
        FailInput(onError);
        return;
    }
    while (open_ && remaining > 0) {
        DWORD requested = static_cast<DWORD>(remaining);
        if (type == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (!::PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) {
                const DWORD error = ::GetLastError();
                if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) EndInput(onLine);
                else FailInput(onError);
                return;
            }
            if (available == 0) return;
            requested = std::min(requested, available);
        }
        DWORD count = 0;
        if (!::ReadFile(handle_, bytes.data(), requested, &count, nullptr)) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF) EndInput(onLine);
            else FailInput(onError);
            return;
        }
        if (count == 0) {
            EndInput(onLine);
            return;
        }
        remaining -= count;
        for (DWORD i = 0; i < count; ++i) Consume(bytes[i], onLine, onError);
    }
#else
    if (descriptor_ < 0) {
        FailInput(onError);
        return;
    }
    while (remaining > 0) {
        pollfd descriptor{descriptor_, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 0);
        if (ready < 0) {
            if (errno != EINTR) FailInput(onError);
            return;
        }
        if (ready == 0) return;
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) {
            if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) FailInput(onError);
            return;
        }
        const ssize_t count = ::read(descriptor_, bytes.data(), remaining);
        if (count < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) FailInput(onError);
            return;
        }
        if (count == 0) {
            EndInput(onLine);
            return;
        }
        remaining -= static_cast<std::size_t>(count);
        for (ssize_t i = 0; i < count; ++i) Consume(bytes[static_cast<std::size_t>(i)], onLine, onError);
    }
#endif
}

} // namespace runtime

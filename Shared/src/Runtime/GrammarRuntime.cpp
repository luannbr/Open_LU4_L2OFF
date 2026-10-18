#include "Shared/src/Runtime/GrammarRuntime.h"

#include "Shared/src/Runtime/WideStreamLog.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

namespace grammar {

void ReportSyntaxError(int line, const char* message) {
    // The original runtime wrote "Syntax error near input line N (token code X)."
    // Bison names the unexpected token instead of the numeric code.
    const std::string text = "Syntax error near input line " + std::to_string(line) + ": " +
        (message != nullptr ? message : "syntax error") + ".";
    runtime::logging::WriteWideDiagnostic(std::wcerr, runtime::logging::Level::Error, text, __FILE__);
}

void ReportError(const wchar_t* message) {
    // The original runtime wrote the message through the parser error stream.
    const char* const sourceFile = runtime::logging::SourceFileScope::Consume();
    runtime::logging::WriteWideDiagnostic(
        std::wcerr, runtime::logging::Level::Error, WideToUtf8(message), *sourceFile != '\0' ? sourceFile : __FILE__
    );
}

void ReportError(const char16_t* message) {
    const char* const sourceFile = runtime::logging::SourceFileScope::Consume();
    runtime::logging::WriteWideDiagnostic(
        std::wcerr, runtime::logging::Level::Error, Utf16ToUtf8(message), *sourceFile != '\0' ? sourceFile : __FILE__
    );
}

void UTF16Scanner::SetInput(const char16_t* data, std::size_t size) {
    cursor_ = data;
    limit_ = data + size;
    marker_ = data;
    ctxmarker_ = data;
    token_ = data;
}

void UTF16Scanner::SetReader(std::function<int()> read) {
    read_ = std::move(read);
    readEnded_ = false;
    buffer_.assign(4096, u'\0');
    SetInput(buffer_.data(), 0);
}

int UTF16Scanner::Fill() {
    if (!read_ || readEnded_) {
        return 1;
    }
    const int value = read_();
    if (value == -1) {
        readEnded_ = true;
        return 1;
    }

    const char16_t* const base = buffer_.data();
    if (limit_ == base + buffer_.size()) {
        // The buffer is full. Keep only the current token. Grow the buffer
        // when the token itself fills it. Old backup positions before the
        // token are not used again, so they move to the token start.
        const auto offset = [this](const char16_t* position) {
            return position > token_ ? static_cast<std::size_t>(position - token_) : std::size_t{0};
        };
        const std::size_t cursor = offset(cursor_);
        const std::size_t marker = offset(marker_);
        const std::size_t ctxmarker = offset(ctxmarker_);
        const std::size_t used = offset(limit_);
        std::copy(token_, limit_, buffer_.begin());
        if (used == buffer_.size()) {
            buffer_.resize(buffer_.size() * 2);
        }
        const char16_t* const moved = buffer_.data();
        token_ = moved;
        cursor_ = moved + cursor;
        marker_ = moved + marker;
        ctxmarker_ = moved + ctxmarker;
        limit_ = moved + used;
    }
    buffer_[static_cast<std::size_t>(limit_ - buffer_.data())] = static_cast<char16_t>(value);
    ++limit_;
    return 0;
}

void UTF16Scanner::EndToken() {
    const std::size_t length = static_cast<std::size_t>(cursor_ - token_);
    text_.assign(token_, cursor_);
    text_.push_back(u'\0');
    yytext = text_.data();
    yyleng = static_cast<int>(length);
    yylineno += static_cast<int>(std::count(token_, cursor_, u'\n'));
}

} // namespace grammar

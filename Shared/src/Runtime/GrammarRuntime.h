#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace grammar {

// Value of one grammar symbol. The original generated parsers kept every
// token and rule value in one eight-byte slot. The bison grammars keep this
// slot, so a value written as one type and read as another type keeps its
// old bits.
using SemanticBits = std::uint64_t;

// Typed views of a value. They copy bits, so an int read takes the low four
// bytes and a pointer read takes all eight bytes, as in the original code.
inline std::int32_t SemanticInt32(SemanticBits bits) {
    const auto low = static_cast<std::uint32_t>(bits);
    std::int32_t value = 0;
    std::memcpy(&value, &low, sizeof(value));
    return value;
}

inline std::int64_t SemanticInt64(SemanticBits bits) {
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline double SemanticDouble(SemanticBits bits) {
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline float SemanticFloat(SemanticBits bits) {
    const auto low = static_cast<std::uint32_t>(bits);
    float value = 0.0F;
    std::memcpy(&value, &low, sizeof(value));
    return value;
}

template <typename T> T* SemanticPointer(SemanticBits bits) {
    return reinterpret_cast<T*>(static_cast<std::uintptr_t>(bits));
}

inline char16_t* SemanticString(SemanticBits bits) { return SemanticPointer<char16_t>(bits); }

// Values to store in a slot. The low-part writes change only the low four
// bytes and keep the rest of the slot, as the original four-byte stores did.
inline void SetLow32(SemanticBits& slot, std::int32_t value) {
    std::uint32_t low = 0;
    std::memcpy(&low, &value, sizeof(low));
    slot = (slot & 0xFFFFFFFF00000000ULL) | low;
}

inline void SetLowFloat(SemanticBits& slot, float value) {
    std::uint32_t low = 0;
    std::memcpy(&low, &value, sizeof(low));
    slot = (slot & 0xFFFFFFFF00000000ULL) | low;
}

inline SemanticBits Int64Semantic(std::int64_t value) {
    SemanticBits bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline SemanticBits DoubleSemantic(double value) {
    SemanticBits bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

template <typename T> SemanticBits PointerSemantic(T* value) {
    return static_cast<SemanticBits>(reinterpret_cast<std::uintptr_t>(value));
}

// Bison uses the token numbers 256 (error) and 257 (undefined). Named tokens
// start at 258. A scanner returns one character as its own code. A character
// at or above 256 becomes the undefined token, so it still causes a syntax
// error and never matches a named token by accident.
constexpr int kUndefinedToken = 257;

constexpr int CharacterToken(char16_t value) { return value < 256 ? static_cast<int>(value) : kUndefinedToken; }

// Default syntax error report for a parser without its own handler. It writes
// to the standard wide error stream, as the original parser runtime did.
void ReportSyntaxError(int line, const char* message);

// Error report of a grammar action, as the yyerror function of the original
// parser runtime. It writes the message to the standard wide error stream and
// does not count a syntax error.
void ReportError(const wchar_t* message);
void ReportError(const char16_t* message);

// Input and token state for the re2c scanners. The scanners read UTF-16 code
// units (re2c --ucs2). Every code unit is one character, as in the original
// wide lexers. The re2c configuration is in UTF16Scanner.re.
class UTF16Scanner {
public:
    UTF16Scanner() = default;
    UTF16Scanner(const UTF16Scanner&) = delete;
    UTF16Scanner& operator=(const UTF16Scanner&) = delete;

    // Start conditions keep their original values: 0, 2, 4 and so on.
    void yybegin(int condition) { yystart = condition; }

    int yystart = 0;
    // Copy of the current token with a terminating NUL. Actions may change it.
    char16_t* yytext = nullptr;
    int yyleng = 0;
    // One plus the number of line feeds up to the end of the current token.
    int yylineno = 1;
    // Stream input for the scanners that read a file stream.
    std::wistream* yyin = nullptr;

protected:
    // Scan a buffer that the caller keeps alive while the scanner runs.
    void SetInput(const char16_t* data, std::size_t size);

    // Read the input with `read`, one code unit at a time, while the scanner
    // runs. The value -1 ends the input. The scanner reads a code unit only
    // when it needs it, as the original lexers did, so a reader with side
    // effects sees the same calls.
    void SetReader(std::function<int()> read);

    bool HasInput() const { return limit_ != nullptr; }

    // Read one code unit from yyin, as the default reader of the original
    // lexers did. A failed read ends the input. The value keeps only the low
    // 16 bits of the wide character.
    int ReadStream() {
        const std::wistream::int_type value = yyin->get();
        return yyin->good() ? static_cast<std::uint16_t>(value) : -1;
    }

    // re2c YYFILL. Add one code unit from the reader. Return 0 on success.
    int Fill();

    // Primitives for the re2c custom input API.
    char16_t Peek() const { return cursor_ < limit_ ? *cursor_ : u'\0'; }
    void BeginToken() { token_ = cursor_; }
    // Publish yytext and yyleng for the matched token and count its line feeds.
    void EndToken();

    const char16_t* cursor_ = nullptr;
    const char16_t* limit_ = nullptr;
    const char16_t* marker_ = nullptr;
    const char16_t* ctxmarker_ = nullptr;
    const char16_t* token_ = nullptr;

private:
    std::function<int()> read_;
    bool readEnded_ = false;
    // Reader input from the start of the current token.
    std::vector<char16_t> buffer_;
    std::vector<char16_t> text_;
};

} // namespace grammar

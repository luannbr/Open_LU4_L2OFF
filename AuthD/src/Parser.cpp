#include "Parser.h"

#include "Log.h"

// L2Authd.exe 0x0046C4F0, Src/parser.cpp:20-22.
bool IsDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

// L2Authd.exe 0x0046C520, Src/parser.cpp:30-32.
bool IsWhiteSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

// L2Authd.exe 0x0046C4B0, Src/parser.cpp:35-37.
bool IsAlpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

namespace {

bool IsLegacyWordByte(unsigned char c) noexcept {
    // The 2019 parser treats the complete 0xA1..0xFD Wansung byte range as
    // word data. This byte-oriented behavior is intentional and independent
    // of the host locale.
    return c > 0xa0U && c < 0xfeU;
}

void ExtractByte(
    std::istream& stream,
    unsigned char& c) {
    stream >> c;
}

} // namespace

// L2Authd.exe 0x0046BB60, Src/parser.cpp:40-139.
Token GetToken(
    std::istream& stream,
    char* value,
    bool strOnly) {
    if (stream.eof()) {
        return eEOF;
    }

    unsigned char c = 0;
    do {
        ExtractByte(stream, c);
        if (!IsWhiteSpace(static_cast<char>(c))) {
            break;
        }
    } while (!stream.eof());

    if (stream.eof()) {
        return eEOF;
    }

    switch (c) {
    case '\n':
        return eNewLine;
    case '&':
        return eAmpersand;
    case '\'':
        return eSingleQuot;
    case '(':
        return eLeftParen;
    case ')':
        return eRightParen;
    case ',':
        return eComma;
    case '.':
        return ePeriod;
    case '/':
        return eSlash;
    case ':':
        return eColon;
    case ';':
        return eSemiColon;
    case '=':
        return eEqual;
    default:
        break;
    }

    char* valPtr = value;
    int count = 0;

    if (c == '-') {
        ExtractByte(stream, c);
        if (!IsDigit(static_cast<char>(c))) {
            stream.putback(static_cast<char>(c));
            return eMinus;
        }
        *valPtr++ = '-';
        ++count;
    }

    if (IsDigit(static_cast<char>(c))) {
        do {
            *valPtr++ = static_cast<char>(c);
            ++count;
            ExtractByte(stream, c);
        } while (IsDigit(static_cast<char>(c)) &&
                 count < 0x800 &&
                 !stream.eof());

        if (!stream.eof()) {
            stream.putback(static_cast<char>(c));
        }
        *valPtr = '\0';
        return strOnly ? eWord : eNumber;
    }

    if (c == '"') {
        ExtractByte(stream, c);
        while (c != '"' &&
               c != '\n' &&
               count < 0x800 &&
               !stream.eof()) {
            *valPtr++ = static_cast<char>(c);
            ++count;
            ExtractByte(stream, c);
        }
        if (c == '\n') {
            stream.putback('\n');
        }
        *valPtr = '\0';
        return eWord;
    }

    if (IsAlpha(static_cast<char>(c)) ||
        IsLegacyWordByte(c)) {
        do {
            *valPtr++ = static_cast<char>(c);
            ExtractByte(stream, c);
        } while ((IsAlpha(static_cast<char>(c)) ||
                  IsLegacyWordByte(c) ||
                  c == '_' ||
                  IsDigit(static_cast<char>(c))) &&
                 !stream.eof());

        if (!stream.eof()) {
            stream.putback(static_cast<char>(c));
            if (stream.bad()) {
                // Log.AddLog(LOG_ERROR, "putback")
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=config event=token_read_failed operation=putback stream_bad=%d stream_failed=%d action=return_current_word",
                //     stream.bad(), stream.fail())
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[config] Could not put a byte back while reading a token (stream bad: %s. Stream failed: %s). Returning the current word.",
                //     stream.bad() ? "yes" : "no", stream.fail() ? "yes" : "no")
                L2_LOG_ERROR(Log,
                    "[config] Could not put a byte back while reading a token (stream bad: %s; stream failed: %s). Returning the current word.",
                    stream.bad() ? "yes" : "no", stream.fail() ? "yes" : "no");
            }
        }
        *valPtr = '\0';
        return eWord;
    }

    // Log.AddLog(
    //     LOG_ERROR,
    //     "unexpected char '%c'",
    //     static_cast<int>(c))
    // Log.AddLog(
    //     LOG_ERROR,
    //     "component=config event=token_read_failed reason=unexpected_byte string_only=%d input=redacted action=return_error_token",
    //     strOnly)
    // Log.AddLog(
    //     LOG_ERROR,
    //     "[config] Unexpected byte while reading a token (string-only mode %s). Returning an error token. Input is omitted.",
    //     strOnly ? "enabled" : "disabled")
    L2_LOG_ERROR(Log,
        "[config] Unexpected byte while reading a token (string-only mode %s). Returning an error token; input is omitted.",
        strOnly ? "enabled" : "disabled");
    value[0] = static_cast<char>(c);
    value[1] = '\0';
    return eError;
}

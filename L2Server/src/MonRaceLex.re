// CLexerForMonRace scanner (original L2Server/monrace_lex.l).
// re2c generates MonRaceLex.cpp.

#include "L2Server/src/MonRaceLex.h"

#include "L2Server/src/MonRaceParser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "MonRaceParserTokens.h"

namespace {

std::string NarrowAscii(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return {};
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (errno == ERANGE) {
        return parsed < 0 ? INT64_MIN : INT64_MAX;
    }
    return static_cast<std::int64_t>(parsed);
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
    const int destinationLength = length - 1;
    auto* const duplicate =
        static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text + 1, length - 2, duplicate);
    duplicate[length - 2] = u'\0';
    return duplicate;
}

} // namespace

CLexerForMonRace::CLexerForMonRace() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForMonRace::~CLexerForMonRace() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForMonRace::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForMonRace::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "mon_begin" { EndToken(); return TK_MON_BEGIN; }
            "mon_end" { EndToken(); return TK_MON_END; }
            "track_begin" { EndToken(); return TK_TRACK_BEGIN; }
            "track_end" { EndToken(); return TK_TRACK_END; }
            "sys_string" { EndToken(); return TK_SYS_STRING; }
            "min_speed" { EndToken(); return TK_MIN_SPEED; }
            "max_speed" { EndToken(); return TK_MAX_SPEED; }
            "guts" { EndToken(); return TK_GUTS; }
            "initial_cond" { EndToken(); return TK_INITIAL_COND; }
            "initial_win" { EndToken(); return TK_INITIAL_WIN; }
            "initial_runcount" { EndToken(); return TK_INITIAL_RUNCOUNT; }
            "initial_wincount" { EndToken(); return TK_INITIAL_WINCOUNT; }
            "race_begin" { EndToken(); return TK_RACE_BEGIN; }
            "return_rate" { EndToken(); return TK_RETURN_RATE; }
            "begin_music" { EndToken(); return TK_BEGIN_MUSIC; }
            "begin_sound" { EndToken(); return TK_BEGIN_SOUND; }
            "race_area" { EndToken(); return TK_RACE_AREA; }
            "residenceid" { EndToken(); return TK_RESIDENCEID; }
            "race_end" { EndToken(); return TK_RACE_END; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(ParseWindowsInt64(std::u16string_view(yytext, yyleng)));
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval =
                    static_cast<grammar::SemanticBits>(ParseWindowsInt64(std::u16string_view(yytext + 1, yyleng - 1)));
                return TK_INTEGER;
            }
            [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateInnerText(yytext, yyleng));
                return TK_NAME;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . { EndToken(); continue; }
            $ { return 0; }
        */
    }
}

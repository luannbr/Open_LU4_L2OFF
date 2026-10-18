// CLexerForCubic scanner (original L2Server/lexer_cubic.l).
// re2c generates LexerCubic.cpp.

#include "L2Server/src/LexerCubic.h"

#include "L2Server/src/ParserCubic.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserCubicTokens.h"

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

grammar::SemanticBits ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    if (end == ascii.c_str()) {
        return 0;
    }
    grammar::SemanticBits bits = 0;
    static_assert(sizeof(bits) == sizeof(parsed));
    std::memcpy(&bits, &parsed, sizeof(bits));
    return bits;
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

CLexerForCubic::CLexerForCubic() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForCubic::~CLexerForCubic() { m_FileBuf = nullptr; }

int CLexerForCubic::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForCubic::Lex(grammar::SemanticBits* lval) {
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
            "cubic_begin" { EndToken(); return TK_CUBIC_BEGIN; }
            "cubic_end" { EndToken(); return TK_CUBIC_END; }
            "id" { EndToken(); return TK_ID; }
            "level" { EndToken(); return TK_LEVEL; }
            "slot" { EndToken(); return TK_SLOT; }
            "duration" { EndToken(); return TK_DURATION; }
            "delay" { EndToken(); return TK_DELAY; }
            "max_count" { EndToken(); return TK_MAX_COUNT; }
            "power" { EndToken(); return TK_POWER; }
            "target_type" { EndToken(); return TK_TARGET_TYPE; }
            "master" { EndToken(); return TK_MASTER; }
            "heal" { EndToken(); return TK_HEAL; }
            "target" { EndToken(); return TK_TARGET; }
            "by_skill" { EndToken(); return TK_BY_SKILL; }
            "op_cond" { EndToken(); return TK_OP_COND; }
            "debuff" { EndToken(); return TK_DEBUFF; }
            "skill1" { EndToken(); return TK_SKILL1; }
            "skill2" { EndToken(); return TK_SKILL2; }
            "skill3" { EndToken(); return TK_SKILL3; }
            "timeskill1" { EndToken(); return TK_TIMESKILL1; }
            "timeskill2" { EndToken(); return TK_TIMESKILL2; }
            "timeskill3" { EndToken(); return TK_TIMESKILL3; }
            "timeskill4" { EndToken(); return TK_TIMESKILL4; }
            "timeskill5" { EndToken(); return TK_TIMESKILL5; }
            "agathion_begin" { EndToken(); return TK_AGATHION_BEGIN; }
            "agathion_end" { EndToken(); return TK_AGATHION_END; }
            "npc_name" { EndToken(); return TK_NPC_NAME; }
            "use_up" { EndToken(); return TK_USE_UP; }
            "item_ids" { EndToken(); return TK_ITEM_IDS; }
            "energy" { EndToken(); return TK_ENERGY; }
            "max_energy" { EndToken(); return TK_MAX_ENERGY; }
            "daily" { EndToken(); return TK_DAILY; }
            "period" { EndToken(); return TK_PERIOD; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
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

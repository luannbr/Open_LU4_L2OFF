// skillenchantdata.txt scanner (original L2Server/lexer_skillenchantdata.l).
// re2c generates LexerSkillEnchantData.cpp.

#include "L2Server/src/LexerSkillEnchantData.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserSkillEnchantData.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserSkillEnchantDataTokens.h"

namespace {

// Current L2Server 0x015ff09c. The generated stream lexer consumes the
// UTF-16 byte-order mark only on its first input stream in the process.
bool firstTime = true;

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

std::uint64_t ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    double parsed = 0.0;
    if (!ascii.empty() || value.empty()) {
        char* end = nullptr;
        parsed = std::strtod(ascii.c_str(), &end);
        if (end == ascii.c_str()) {
            parsed = 0.0;
        }
    }
    std::uint64_t bits = 0;
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

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForSkillEnchant::CLexerForSkillEnchant() : line(0) {
}

int CLexerForSkillEnchant::Create() { return 1; }

int CLexerForSkillEnchant::yygetchar() {
    if (firstTime) {
        yyin->get();
        firstTime = false;
    }
    return ReadStream();
}

int CLexerForSkillEnchant::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "/*" .* "*/" { EndToken(); continue; }
            "enchant_skill_begin" { EndToken(); return TK_ENCHANT_SKILL_BEGIN; }
            "enchant_skill_end" { EndToken(); return TK_ENCHANT_SKILL_END; }
            "original_skill" { EndToken(); return TK_ORIGINAL_SKILL; }
            "route_id" { EndToken(); return TK_ROUTE_ID; }
            "enchant_id" { EndToken(); return TK_ENCHANT_ID; }
            "skill_sub_level" { EndToken(); return TK_SKILL_SUB_LEVEL; }
            "required_sp" { EndToken(); return TK_REQUIRED_SP; }
            "required_item" { EndToken(); return TK_REQUIRED_ITEM; }
            "importance" { EndToken(); return TK_IMPORTANCE; }
            "enchant_prob" { EndToken(); return TK_ENCHANT_PROB; }
            "item_needed" { EndToken(); return TK_ITEM_NEEDED; }
            "prob_76" { EndToken(); return TK_PROB_76; }
            "prob_77" { EndToken(); return TK_PROB_77; }
            "prob_78" { EndToken(); return TK_PROB_78; }
            "skill_level" { EndToken(); return TK_SKILL_LEVEL; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            [&;={}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                *lval = reinterpret_cast<std::uintptr_t>(DuplicateInnerText(yytext, yyleng));
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const std::int64_t itemId = g_PrecompiledHeaderDB.Search(yytext + 1);
                if (itemId == -1) {
                    // const std::wstring hostName = target_utf16::ToHostCodeUnits(yytext + 1)
                    const std::string hostName = Utf16ToUtf8(yytext + 1);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostName.c_str())
                    L2_LOG_ERROR(
                        Log,
                        "skillenchantdata.txt line %lld: PCH item reference '%s' resolves to -1; parsing continues with "
                        "that "
                        "numeric value.",
                        static_cast<long long>(line) + 1, hostName.c_str()
                    );
                }
                *lval = static_cast<grammar::SemanticBits>(itemId);
                return TK_INTEGER;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval = reinterpret_cast<std::uintptr_t>(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                const std::string invalid = Utf16ToUtf8(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log,
                    "skillenchantdata.txt line %lld: invalid input '%s' (first UTF-16 code unit 0x%04X); input skipped.",
                    static_cast<long long>(line) + 1, invalid.c_str(),
                    static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            $ { return 0; }
        */
    }
}

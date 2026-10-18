// teleportquestpath.txt scanner.
// re2c generates LexerTeleportQuestPath.cpp.

#include "L2Server/src/LexerTeleportQuestPath.h"

#include "L2Server/src/ParserTeleportQuestPath.h"

#include "Shared/src/Log.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserTeleportQuestPathTokens.h"

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

std::int32_t ParseWindowsInt32(std::u16string_view value) {
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
    if (errno == ERANGE || parsed > INT32_MAX) {
        return INT32_MAX;
    }
    if (parsed < INT32_MIN) {
        return INT32_MIN;
    }
    return static_cast<std::int32_t>(parsed);
}

} // namespace

CLexerForTeleportQuestPath::CLexerForTeleportQuestPath() {
    comment_count = 0;
    line = 0;
}

CLexerForTeleportQuestPath::~CLexerForTeleportQuestPath() = default;

int CLexerForTeleportQuestPath::Create() { return 1; }

int CLexerForTeleportQuestPath::yygetchar() { return ReadStream(); }

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForTeleportQuestPath::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            re2c:define:YYGETCONDITION = "kConditionByStart[yystart / 2]";
            <COMMENT> "/*" {
                EndToken();
                ++comment_count;
                continue;
            }
            <COMMENT> "*/" {
                EndToken();
                --comment_count;
                if (comment_count == 0) {
                    yybegin(4);
                }
                continue;
            }
            <COMMENT> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <COMMENT> . { EndToken(); continue; }
            <MAIN> "//" [^\n]* / "\n" { EndToken(); continue; }
            <MAIN> "/*" {
                EndToken();
                yybegin(2);
                comment_count = 1;
                continue;
            }
            <MAIN> "teleport_quest_path_begin" { EndToken(); return TK_TELEPORT_QUEST_PATH_BEGIN; }
            <MAIN> "teleport_quest_path_end" { EndToken(); return TK_TELEPORT_QUEST_PATH_END; }
            <MAIN> "npc_id" { EndToken(); return TK_NPC_ID; }
            <MAIN> "npc_loc_zoneid" { EndToken(); return TK_NPC_LOC_ZONEID; }
            <MAIN> "telposlist" { EndToken(); return TK_TELPOSLIST; }
            <INITIAL,COMMENT,MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(
                    ParseWindowsInt32(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                ));
                return TK_INTEGER;
            }
            <INITIAL,MAIN> [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN> . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)),
                //     yylineno)
                // L2_LOG_ERROR(Log,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)),
                //     yylineno)
                L2_LOG_ERROR(
                    Log,
                    "teleportquestpath.txt line %lld: invalid UTF-16 code unit 0x%04X; this input is skipped and lexing "
                    "continues.",
                    static_cast<long long>(line) + 1, static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}

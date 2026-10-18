// CLexerForAction scanner (original L2Server/lexer_basicaction.l).
// re2c generates LexerBasicAction.cpp.

#include "L2Server/src/LexerBasicAction.h"

#include "L2Server/src/ParserBasicAction.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserBasicActionTokens.h"

namespace {

// Current L2Server.exe 0x00e22518..0x00e22540,
// 0x012b5440..0x012b62f4, and 0x013662f8..0x013662fc.

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    std::string ascii;
    ascii.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return 0;
        }
        ascii.push_back(static_cast<char>(codeUnit));
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

} // namespace

CLexerForAction::CLexerForAction() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForAction::~CLexerForAction() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForAction::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN, yycMAIN2};

} // namespace

int CLexerForAction::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "action_begin" { EndToken(); return TK_ACTION_BEGIN; }
            <MAIN> "action_end" { EndToken(); return TK_ACTION_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "handler" { EndToken(); return TK_HANDLER; }
            <MAIN> "option" {
                EndToken();
                yybegin(6);
                return TK_OPTION;
            }
            <MAIN> "allow_raid_user" { EndToken(); return TK_ALLOW_RAID_USER; }
            <MAIN2> "none" {
                EndToken();
                yybegin(4);
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(-1));
                return TK_NONE_GROUP;
            }
            <MAIN2> "change_mode" {
                EndToken();
                yybegin(4);
                *lval = 0;
                return TK_NONE_GROUP;
            }
            <MAIN2> "attack" {
                EndToken();
                yybegin(4);
                *lval = 1;
                return TK_NONE_GROUP;
            }
            <MAIN2> "stop" {
                EndToken();
                yybegin(4);
                *lval = 2;
                return TK_NONE_GROUP;
            }
            <MAIN2> "get_item" {
                EndToken();
                yybegin(4);
                *lval = 3;
                return TK_NONE_GROUP;
            }
            <MAIN2> "skill" {
                EndToken();
                yybegin(4);
                *lval = 4;
                return TK_NONE_GROUP;
            }
            <MAIN2> "move" {
                EndToken();
                yybegin(4);
                *lval = 6;
                return TK_NONE_GROUP;
            }
            <MAIN2> "offensive_ai" {
                EndToken();
                yybegin(4);
                *lval = 7;
                return TK_NONE_GROUP;
            }
            <MAIN2> "defensive_ai" {
                EndToken();
                yybegin(4);
                *lval = 8;
                return TK_NONE_GROUP;
            }
            <MAIN2> "passive_ai" {
                EndToken();
                yybegin(4);
                *lval = 9;
                return TK_NONE_GROUP;
            }
            <MAIN2> "fast_reaction" {
                EndToken();
                yybegin(4);
                *lval = 10;
                return TK_NONE_GROUP;
            }
            <MAIN2> "normal_reaction" {
                EndToken();
                yybegin(4);
                *lval = 11;
                return TK_NONE_GROUP;
            }
            <MAIN2> "slow_reaction" {
                EndToken();
                yybegin(4);
                *lval = 12;
                return TK_NONE_GROUP;
            }
            <MAIN2> "wheel" {
                EndToken();
                yybegin(4);
                *lval = 0;
                return TK_NONE_GROUP;
            }
            <MAIN2> "leave_wheel" {
                EndToken();
                yybegin(4);
                *lval = 1;
                return TK_NONE_GROUP;
            }
            <MAIN2> "start_ship" {
                EndToken();
                yybegin(4);
                *lval = 2;
                return TK_NONE_GROUP;
            }
            <MAIN2> "leave_ship" {
                EndToken();
                yybegin(4);
                *lval = 3;
                return TK_NONE_GROUP;
            }
            <MAIN2> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                yybegin(4);
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_NONE_GROUP;
            }
            <INITIAL,COMMENT,MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_NONE_GROUP;
            }
            <INITIAL,MAIN,MAIN2> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <INITIAL,COMMENT,MAIN,MAIN2> "[" [^[\]]* "]" {
                EndToken();
                const int destinationLength = yyleng - 1;
                auto* const text =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
                if (text != nullptr) {
                    std::copy_n(yytext + 1, yyleng - 2, text);
                    text[yyleng - 2] = u'\0';
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <INITIAL,MAIN,MAIN2> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN,MAIN2> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN,MAIN2> . { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN,MAIN2> $ { return 0; }
        */
    }
}

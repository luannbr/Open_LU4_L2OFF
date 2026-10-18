// CLexerForItemAnnounceInfo scanner.
// re2c generates ItemAnnounceLexer.cpp.

#include "L2Server/src/ItemAnnounceLexer.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ItemAnnounceParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ItemAnnounceParserTokens.h"

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

CLexerForItemAnnounceInfo::CLexerForItemAnnounceInfo() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForItemAnnounceInfo::~CLexerForItemAnnounceInfo() { m_FileBuf = nullptr; }

int CLexerForItemAnnounceInfo::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForItemAnnounceInfo::Lex(grammar::SemanticBits* lval) {
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
            <COMMENT> "\n" { EndToken(); continue; }
            <COMMENT> . { EndToken(); continue; }
            <MAIN> "//" [^\n]* / "\n" { EndToken(); continue; }
            <MAIN> "/*" {
                EndToken();
                yybegin(2);
                comment_count = 1;
                continue;
            }
            <MAIN> "item_begin" { EndToken(); return TK_ITEM_BEGIN; }
            <MAIN> "item_id" { EndToken(); return TK_ITEM_ID; }
            <MAIN> "announce_level" { EndToken(); return TK_ANNOUNCE_LEVEL; }
            <MAIN> "item_end" { EndToken(); return TK_ITEM_END; }
            <MAIN> "random_box_begin" { EndToken(); return TK_RANDOM_BOX_BEGIN; }
            <MAIN> "announce_level_min" { EndToken(); return TK_ANNOUNCE_LEVEL_MIN; }
            <MAIN> "fixed_items" { EndToken(); return TK_FIXED_ITEMS; }
            <MAIN> "random_box_end" { EndToken(); return TK_RANDOM_BOX_END; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> ";" { EndToken(); return TK_SEMICOLON; }
            <MAIN> "{" { EndToken(); return TK_LBRACE; }
            <MAIN> "}" { EndToken(); return TK_RBRACE; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> [&/] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)))
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}

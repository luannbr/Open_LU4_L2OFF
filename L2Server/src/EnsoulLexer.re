// CLexerForEnsoul scanner.
// re2c generates EnsoulLexer.cpp.

#include "L2Server/src/EnsoulLexer.h"

#include "L2Server/src/EnsoulParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "EnsoulParserTokens.h"

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

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
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

CLexerForEnsoul::CLexerForEnsoul() :
    m_bOnlyPrecompiledHeader(false), m_nLine(0), m_nCommentCount(0), m_bReadFirstCharacter(true) {
}

CLexerForEnsoul::~CLexerForEnsoul() = default;

int CLexerForEnsoul::Create(CParserForEnsoul* parser, char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    m_bOnlyPrecompiledHeader = parser->m_bOnlyPrecompiledHeader;
    return 1;
}

void CLexerForEnsoul::Initialize() { yybegin(4); }

int CLexerForEnsoul::GetLine() { return m_nLine; }

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForEnsoul::Lex(grammar::SemanticBits* lval) {
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
                ++m_nCommentCount;
                continue;
            }
            <COMMENT> "*/" {
                EndToken();
                --m_nCommentCount;
                if (m_nCommentCount == 0) {
                    yybegin(4);
                }
                continue;
            }
            <COMMENT> "\n" {
                EndToken();
                ++m_nLine;
                continue;
            }
            <COMMENT> . { EndToken(); continue; }
            <MAIN> "//" [^\n]* / "\n" { EndToken(); continue; }
            <MAIN> "/*" {
                EndToken();
                yybegin(2);
                m_nCommentCount = 1;
                continue;
            }
            <MAIN> "ensoul_option_begin" { EndToken(); return TK_ENSOUL_OPTION_BEGIN; }
            <MAIN> "ensoul_option_end" { EndToken(); return TK_ENSOUL_OPTION_END; }
            <MAIN> "option_id" { EndToken(); return TK_OPTION_ID; }
            <MAIN> "option_name" { EndToken(); return TK_OPTION_NAME; }
            <MAIN> "option_type" { EndToken(); return TK_OPTION_TYPE; }
            <MAIN> "option_skill_list" { EndToken(); return TK_OPTION_SKILL_LIST; }
            <MAIN> "extraction_stone_name" { EndToken(); return TK_EXTRACTION_STONE_NAME; }
            <MAIN> "ensoul_stone_begin" { EndToken(); return TK_ENSOUL_STONE_BEGIN; }
            <MAIN> "ensoul_stone_end" { EndToken(); return TK_ENSOUL_STONE_END; }
            <MAIN> "stone_name" { EndToken(); return TK_STONE_NAME; }
            <MAIN> "slot_type" { EndToken(); return TK_SLOT_TYPE; }
            <MAIN> "ensoul_option_list" { EndToken(); return TK_ENSOUL_OPTION_LIST; }
            <MAIN> "ensoul_fee_begin" { EndToken(); return TK_ENSOUL_FEE_BEGIN; }
            <MAIN> "ensoul_fee_end" { EndToken(); return TK_ENSOUL_FEE_END; }
            <MAIN> "crystal_type" { EndToken(); return TK_CRYSTAL_TYPE; }
            <MAIN> "ensoul_fee_normal" { EndToken(); return TK_ENSOUL_FEE_NORMAL; }
            <MAIN> "ensoul_fee_bm" { EndToken(); return TK_ENSOUL_FEE_BM; }
            <MAIN> "ensoul_refee_normal" { EndToken(); return TK_ENSOUL_REFEE_NORMAL; }
            <MAIN> "ensoul_refee_bm" { EndToken(); return TK_ENSOUL_REFEE_BM; }
            <MAIN> "extraction_fee_normal" { EndToken(); return TK_EXTRACTION_FEE_NORMAL; }
            <MAIN> "extraction_fee_bm" { EndToken(); return TK_EXTRACTION_FEE_BM; }
            <MAIN> "normal" {
                EndToken();
                *lval = 1;
                return TK_NORMAL_BM;
            }
            <MAIN> "bm" {
                EndToken();
                *lval = 2;
                return TK_NORMAL_BM;
            }
            <MAIN> [a-zA-Z][a-zA-Z0-9_\-]* {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_IDENTIFIER;
            }
            <MAIN> [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++m_nLine;
                continue;
            }
            <MAIN> . {
                EndToken();
                // const std::wstring invalid = Utf16ToWide(yytext)
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%s] at line [%d]",
                //     invalid.c_str(),
                //     m_nLine + 1)
                // L2_LOG_ERROR(Log,
                //     L"invalid char [%s] at line [%d]",
                //     invalid.c_str(),
                //     m_nLine + 1)
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(m_nLine) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}

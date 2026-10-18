// multisell.txt scanner (original L2Server/multisell_lex.l).
// re2c generates MultiSellLex.cpp.

#include "L2Server/src/MultiSellLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/MultiSellParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "MultiSellParserTokens.h"

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

double ParseWindowsDouble(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0 : parsed;
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
    if (length < 2) {
        return nullptr;
    }
    const int innerLength = length - 2;
    auto* const duplicate =
        static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(innerLength) + 1U) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text + 1, innerLength, duplicate);
    duplicate[innerLength] = u'\0';
    return duplicate;
}

} // namespace

CLexerForMultiSell::CLexerForMultiSell() :
    m_FileBuf(nullptr), m_RawFileBuf(nullptr), m_FilePointer(0), m_FileSize(0), line(0), comment_count(0) {
}

CLexerForMultiSell::~CLexerForMultiSell() { std::free(m_RawFileBuf); }

int CLexerForMultiSell::Create() {
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\multisell.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/multisell.txt";
#endif
    int byteCount = 0;
    m_RawFileBuf = LoadBinaryAbsoluteSecure(kFileName, byteCount, true);
    if (m_RawFileBuf == nullptr) {
        return 0;
    }
    m_FilePointer = 0;
    m_FileBuf = reinterpret_cast<char16_t*>(m_RawFileBuf) + 1;
    m_FileSize = byteCount / 2 - 1;
    return 1;
}

int CLexerForMultiSell::Create(char16_t* fileBuffer, int fileSize) {
    m_FilePointer = 0;
    m_FileBuf = fileBuffer;
    m_FileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForMultiSell::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_FilePointer,
            m_FilePointer < m_FileSize ? static_cast<std::size_t>(m_FileSize - m_FilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
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
            <MAIN> "MultiSell_begin" { EndToken(); return TK_MULTISELL_BEGIN; }
            <MAIN> "selllist" { EndToken(); return TK_SELLLIST; }
            <MAIN> "is_show_all" { EndToken(); return TK_IS_SHOW_ALL; }
            <MAIN> "keep_enchanted" { EndToken(); return TK_KEEP_ENCHANTED; }
            <MAIN> "show_variation_item" { EndToken(); return TK_SHOW_VARIATION_ITEM; }
            <MAIN> "keep_shape_shifted" { EndToken(); return TK_KEEP_SHAPE_SHIFTED; }
            <MAIN> "show_ensoul_item" { EndToken(); return TK_SHOW_ENSOUL_ITEM; }
            <MAIN> "is_dutyfree" { EndToken(); return TK_IS_DUTYFREE; }
            <MAIN> "show_type" { EndToken(); return TK_SHOW_TYPE; }
            <MAIN> "ssq_flag" { EndToken(); return TK_SSQ_FLAG; }
            <MAIN> "required_npc" { EndToken(); return TK_REQUIRED_NPC; }
            <MAIN> "MultiSell_end" { EndToken(); return TK_MULTISELL_END; }
            <MAIN> "is_without_npc" { EndToken(); return TK_IS_WITHOUT_NPC; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                const char16_t* const number = yytext;
                const int length = yyleng;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(length)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                const char16_t* const number = yytext + 1;
                const int length = yyleng - 1;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(length)))
                );
                return TK_INTEGER;
            }
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(text);
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

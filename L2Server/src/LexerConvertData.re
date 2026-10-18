// convertdata.txt scanner (original L2Server/lexer_convertdata.l).
// re2c generates LexerConvertData.cpp.

#include "L2Server/src/LexerConvertData.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserConvertData.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "ParserConvertDataTokens.h"

namespace {

} // namespace

CLexerForConvertData::CLexerForConvertData() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForConvertData::~CLexerForConvertData() { std::free(m_pRawFileBuf); }

int CLexerForConvertData::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForConvertData::Create() {
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\convertdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/convertdata.txt";
#endif

    int byteCount = 0;
    m_pRawFileBuf = LoadBinaryAbsoluteSecure(kFileName, byteCount, true);
    if (m_pRawFileBuf == nullptr) {
        return 0;
    }
    return Create(reinterpret_cast<char16_t*>(m_pRawFileBuf) + 1, byteCount / 2 - 1);
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForConvertData::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "convert_begin" { EndToken(); return TK_CONVERT_BEGIN; }
            <MAIN> "convert_end" { EndToken(); return TK_CONVERT_END; }
            <MAIN> "input_item" { EndToken(); return TK_INPUT_ITEM; }
            <MAIN> "output_item" { EndToken(); return TK_OUTPUT_ITEM; }
            <MAIN> [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                const int destinationLength = yyleng - 1;
                char16_t* const text =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
                if (text != nullptr) {
                    std::copy_n(yytext + 1, yyleng - 2, text);
                    text[yyleng - 2] = u'\0';
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
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
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

// CLexerForRestrictArea scanner (original L2Server/lexer_restrict_area.l).
// re2c generates LexerRestrictArea.cpp.

#include "L2Server/src/LexerRestrictArea.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserRestrictArea.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserRestrictAreaTokens.h"

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

std::uint64_t ParseWindowsUInt64(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }

    errno = 0;
    char* end = nullptr;
    return static_cast<std::uint64_t>(std::strtoull(ascii.c_str(), &end, 10));
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

CLexerForRestrictArea::CLexerForRestrictArea() {
    m_FileBuf = nullptr;
    m_nFilePointer = 0;
    m_nFileSize = 0;
    line = 0;
}

CLexerForRestrictArea::~CLexerForRestrictArea() { m_FileBuf = nullptr; }

int CLexerForRestrictArea::Create(char16_t* const fileBuffer, const int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForRestrictArea::Lex(grammar::SemanticBits* lval) {
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
            "nofly_begin" { EndToken(); return TK_NOFLY_BEGIN; }
            "nofly_end" { EndToken(); return TK_NOFLY_END; }
            "no_change_pc_begin" { EndToken(); return TK_NO_CHANGE_PC_BEGIN; }
            "no_change_pc_end" { EndToken(); return TK_NO_CHANGE_PC_END; }
            "no_call_pc_begin" { EndToken(); return TK_NO_CALL_PC_BEGIN; }
            "no_call_pc_end" { EndToken(); return TK_NO_CALL_PC_END; }
            "no_drop_item_begin" { EndToken(); return TK_NO_DROP_ITEM_BEGIN; }
            "no_drop_item_end" { EndToken(); return TK_NO_DROP_ITEM_END; }
            "no_save_bookmark_begin" { EndToken(); return TK_NO_SAVE_BOOKMARK_BEGIN; }
            "no_save_bookmark_end" { EndToken(); return TK_NO_SAVE_BOOKMARK_END; }
            "no_use_bookmark_begin" { EndToken(); return TK_NO_USE_BOOKMARK_BEGIN; }
            "no_use_bookmark_end" { EndToken(); return TK_NO_USE_BOOKMARK_END; }
            "transformable_begin" { EndToken(); return TK_TRANSFORMABLE_BEGIN; }
            "transformable_end" { EndToken(); return TK_TRANSFORMABLE_END; }
            "system_msg_id" { EndToken(); return TK_SYSTEM_MSG_ID; }
            "name" { EndToken(); return TK_NAME; }
            "range" { EndToken(); return TK_RANGE; }
            "territory_begin" { EndToken(); return TK_TERRITORY_BEGIN; }
            "territory_end" { EndToken(); return TK_TERRITORY_END; }
            "server_block_begin" { EndToken(); return TK_SERVER_BLOCK_BEGIN; }
            "server_block_end" { EndToken(); return TK_SERVER_BLOCK_END; }
            "min" { EndToken(); return TK_MIN_KW; }
            "max" { EndToken(); return TK_MAX_KW; }
            "announce_area_begin" { EndToken(); return TK_ANNOUNCE_AREA_BEGIN; }
            "announce_area_end" { EndToken(); return TK_ANNOUNCE_AREA_END; }
            "-"? [0-9]+ {
                EndToken();
                *lval = ParseWindowsUInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_INTEGER;
            }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME_2;
            }
            [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
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
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(yylineno)
                );
                continue;
            }
            $ { return 0; }
        */
    }
}

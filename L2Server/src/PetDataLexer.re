// PetDataLexer scanner (original L2Server/petdatalexer.l).
// re2c generates PetDataLexer.cpp.

#include "L2Server/src/PetDataLexer.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/PetDataParser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

#include "PetDataParserTokens.h"

namespace {

std::string ToAscii(std::u16string_view value) {
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
    const std::string ascii = ToAscii(value);
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
    const std::string ascii = ToAscii(value);
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

char16_t* DuplicateWindowsWide(std::u16string_view value) {
    auto* const result = static_cast<char16_t*>(std::malloc((value.size() + 1) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy(value.begin(), value.end(), result);
    result[value.size()] = u'\0';
    return result;
}

} // namespace

PetDataLexer::PetDataLexer() : m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0) {
}

PetDataLexer::~PetDataLexer() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int PetDataLexer::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

const char16_t* PetDataLexer::GetFileName() const { return m_FileName; }

int PetDataLexer::Lex(grammar::SemanticBits* lval) {
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
            "pet_stat_begin" { EndToken(); return TK_PET_STAT_BEGIN; }
            "pet_stat_end" { EndToken(); return TK_PET_STAT_END; }
            "stat_begin" { EndToken(); return TK_STAT_BEGIN; }
            "stat_end" { EndToken(); return TK_STAT_END; }
            "pet_index" { EndToken(); return TK_PET_INDEX; }
            "npc_name" { EndToken(); return TK_NPC_NAME; }
            "item" { EndToken(); return TK_ITEM; }
            "sync_level" { EndToken(); return TK_SYNC_LEVEL; }
            "evolve" { EndToken(); return TK_EVOLVE; }
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
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval = grammar::PointerSemantic(
                    DuplicateWindowsWide(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_IDENTIFIER;
            }
            "[" [^[\]]* "]" {
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
            [;={}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" { EndToken(); continue; }
            . {
                EndToken();
                // std::wprintf(L"invalid chracter=%ls\n", yytext)
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

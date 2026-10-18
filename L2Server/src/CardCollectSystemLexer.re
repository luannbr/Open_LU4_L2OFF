// CLexerForCardCollectSystem scanner.
// re2c generates CardCollectSystemLexer.cpp.

#include "L2Server/src/CardCollectSystemLexer.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/CardCollectSystemParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "CardCollectSystemParserTokens.h"

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

char16_t* DuplicateInnerText(const char16_t* text, const int length) {
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

CLexerForCardCollectSystem::CLexerForCardCollectSystem() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), m_bOnlyPrecompiledHeader(false), line(0) {
}

CLexerForCardCollectSystem::~CLexerForCardCollectSystem() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForCardCollectSystem::Create(CParserForCardCollectSystem* parser, char16_t* fileBuffer, const int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    m_bOnlyPrecompiledHeader = parser->m_bOnlyPrecompiledHeader;
    return 1;
}

int CLexerForCardCollectSystem::Lex(grammar::SemanticBits* lval) {
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
            "card_collect_begin" { EndToken(); return TK_CARD_COLLECT_BEGIN; }
            "card_collect_end" { EndToken(); return TK_CARD_COLLECT_END; }
            "id" { EndToken(); return TK_ID; }
            "source_card_list" { EndToken(); return TK_SOURCE_CARD_LIST; }
            "fixed_reward_list" { EndToken(); return TK_FIXED_REWARD_LIST; }
            "random_reward_list" { EndToken(); return TK_RANDOM_REWARD_LIST; }
            "random_reward_second_list" { EndToken(); return TK_RANDOM_REWARD_SECOND_LIST; }
            "card_collect_event_begin" { EndToken(); return TK_CARD_COLLECT_EVENT_BEGIN; }
            "card_collect_event_end" { EndToken(); return TK_CARD_COLLECT_EVENT_END; }
            "event_id" { EndToken(); return TK_EVENT_ID; }
            "event_start" { EndToken(); return TK_EVENT_START; }
            "event_end" { EndToken(); return TK_EVENT_END; }
            "event_type" { EndToken(); return TK_EVENT_TYPE; }
            "collect_id_list" { EndToken(); return TK_COLLECT_ID_LIST; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(
                    ParseWindowsInt32(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                ));
                return TK_INTEGER;
            }
            "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const std::int64_t value = g_PrecompiledHeaderDB.Search(yytext + 1);
                if (value == -1 && !m_bOnlyPrecompiledHeader) {
                    // const std::wstring word = target_utf16::ToHostCodeUnits(yytext)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Undefined word[%s] in precompiled header",
                    //     word.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"Undefined word[%s] in precompiled header",
                    //     word.c_str())
                    L2_LOG_ERROR(
                        Log, "Undefined precompiled-header name '%s' at script line %lld; passing -1 to the parser.",
                        Utf16ToUtf8(std::u16string_view(yytext + 1)).c_str(), static_cast<long long>(line) + 1
                    );
                }
                *lval = static_cast<grammar::SemanticBits>(value);
                return TK_INTEGER;
            }
            "=" { EndToken(); return TK_EQUAL; }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            [%(),;[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%s] at line [%d]",
                //     invalid.c_str(),
                //     line + 1)
                // L2_LOG_ERROR(Log,
                //     L"invalid char [%s] at line [%d]",
                //     invalid.c_str(),
                //     line + 1)
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            $ { return 0; }
        */
    }
}

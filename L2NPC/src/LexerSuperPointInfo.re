// superpointinfo.txt scanner (original L2NPC/lexer_superpointinfo.l).
// re2c generates LexerSuperPointInfo.cpp.

#include "L2NPC/src/LexerSuperPointInfo.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <istream>
#include <limits>
#include <string>
#include <string_view>

#include "ParserSuperPointInfoTokens.h"

namespace l2geo {
namespace {

// lexer_superpointinfo.l::yygetchar owns a process-static BOM flag.
bool g_FirstSuperPointInfoInput = true;

int TargetWtoi(const char16_t* value) {
    // Current L2NPC.exe 0x00557564 enters the VS2010 signed 32-bit
    // wcstoxl implementation. Read the lexer's null-terminated UTF-16
    // buffer directly. Host wchar_t/long widths are not its ABI.
    while (std::iswspace(static_cast<std::wint_t>(*value)) != 0) {
        ++value;
    }

    bool negative = false;
    if (*value == u'-' || *value == u'+') {
        negative = *value == u'-';
        ++value;
    }

    const std::uint32_t limit = negative ? 0x80000000U : 0x7fffffffU;
    std::uint32_t result = 0;
    bool overflow = false;
    bool converted = false;
    for (; *value >= u'0' && *value <= u'9'; ++value) {
        converted = true;
        const std::uint32_t digit = static_cast<std::uint32_t>(*value - u'0');
        if (result > (limit - digit) / 10U) {
            overflow = true;
        } else if (!overflow) {
            result = result * 10U + digit;
        }
    }

    if (!converted) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return negative ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
    }
    if (!negative) {
        return static_cast<int>(result);
    }
    if (result == 0x80000000U) {
        return std::numeric_limits<int>::min();
    }
    return -static_cast<int>(result);
}

} // namespace

CLexerForSuperPointInfo::CLexerForSuperPointInfo() = default;

CLexerForSuperPointInfo::~CLexerForSuperPointInfo() = default;

int CLexerForSuperPointInfo::create() { return 1; }

int CLexerForSuperPointInfo::yygetchar() {
    if (!yyin) {
        return -1;
    }
    if (g_FirstSuperPointInfoInput) {
        yyin->get();
        yyin->get();
        g_FirstSuperPointInfoInput = false;
    }

    const int low = yyin->get();
    if (yyin->eof()) {
        return -1;
    }
    const int high = yyin->get();
    if (yyin->eof()) {
        return -1;
    }
    return static_cast<int>(
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint16_t>(low))
    );
}

int CLexerForSuperPointInfo::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    void* const semanticSlot = lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "/*" .* "*/" { EndToken(); continue; }
            "superpoint_begin" { EndToken(); return TK_SUPERPOINT_BEGIN; }
            "superpoint_end" { EndToken(); return TK_SUPERPOINT_END; }
            "superpoint_name" { EndToken(); return TK_SUPERPOINT_NAME; }
            "npc_begin" { EndToken(); return TK_NPC_BEGIN; }
            "npc_end" { EndToken(); return TK_NPC_END; }
            "npc_name" { EndToken(); return TK_NPC_NAME; }
            "move_type" { EndToken(); return TK_MOVE_TYPE; }
            "node_begin" { EndToken(); return TK_NODE_BEGIN; }
            "node_end" { EndToken(); return TK_NODE_END; }
            "node_id" { EndToken(); return TK_NODE_ID; }
            "fstring_index" { EndToken(); return TK_FSTRING_INDEX; }
            "social_number" { EndToken(); return TK_SOCIAL_NUMBER; }
            "delay" { EndToken(); return TK_DELAY; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                const int value = TargetWtoi(yytext);
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                const int value = TargetWtoi(yytext + 1);
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_INTEGER;
            }
            [&;={}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                const int sourceLength = yyleng;
                const std::size_t destinationLength = static_cast<std::size_t>(sourceLength - 1);
                char16_t* const text = static_cast<char16_t*>(std::malloc(destinationLength * sizeof(char16_t)));
                if (text) {
                    std::copy(yytext + 1, yytext + sourceLength - 1, text);
                    text[sourceLength - 2] = u'\0';
                    const std::uint64_t address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(text));
                    std::memcpy(semanticSlot, &address, sizeof(address));
                }
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const char16_t* const name = yytext + 1;
                const std::uint32_t value = static_cast<std::uint32_t>(g_PrecompiledHeaderDB.Search(name));
                if (value == 0xffffffffU) {
                    // #if defined(_WIN32)
                    //             const wchar_t* const printable =
                    //                 reinterpret_cast<const wchar_t*>(name)
                    // #else
                    //             const std::wstring printable =
                    //                 Utf16ToWide(name)
                    // #endif
                    //             Log.Add(
                    //                 LOG_ERROR,
                    //                 L"line[%d] : Undefined item name[%s]",
                    //                 line + 1,
                    // #if defined(_WIN32)
                    //                 printable)
                    // #else
                    //                 printable.c_str())
                    // #endif
                    // Log.Add(LOG_ERROR,
                    //     "[NPC route] superpointinfo.txt line %lld: unknown script symbol \"%s\". Continuing with
                    //     identifier 0xffffffff.", static_cast<long long>(line) + 1, Utf16ToUtf8(name).c_str())
                    L2_LOG_ERROR(
                        Log,
                        "[NPC route] superpointinfo.txt line %lld: unknown script symbol \"%s\"; continuing with identifier "
                        "0xffffffff.",
                        static_cast<long long>(line) + 1, Utf16ToUtf8(name).c_str()
                    );
                }
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_INTEGER;
            }
            [a-zA-Z_][a-zA-Z0-9_]* { EndToken(); continue; }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // #if defined(_WIN32)
                //         const wchar_t* const printable =
                //             reinterpret_cast<const wchar_t*>(yytext)
                // #else
                //         const std::wstring printable =
                //             Utf16ToWide(yytext)
                // #endif
                //         Log.Add(
                //             LOG_ERROR,
                //             L"[LEXER_ERROR] Invalid char = %s",
                // #if defined(_WIN32)
                //             printable)
                // #else
                //             printable.c_str())
                // #endif
                // Log.Add(LOG_ERROR,
                //     "[NPC route] superpointinfo.txt line %lld: invalid token \"%s\". Discarding it.",
                //     static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str())
                L2_LOG_ERROR(
                    Log, "[NPC route] superpointinfo.txt line %lld: invalid token \"%s\"; discarding it.",
                    static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str()
                );
                continue;
            }
            $ { return 0; }
        */
    }
}

} // namespace l2geo

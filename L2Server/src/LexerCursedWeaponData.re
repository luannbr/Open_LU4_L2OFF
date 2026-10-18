// cursedweapondata.txt scanner (original L2Server/lexer_cursedweapondata.l).
// re2c generates LexerCursedWeaponData.cpp.

#include "L2Server/src/LexerCursedWeaponData.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserCursedWeaponData.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserCursedWeaponDataTokens.h"

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

std::int32_t ParseWindowsInt(std::u16string_view value) {
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

CLexerForCursedWeapon::CLexerForCursedWeapon() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForCursedWeapon::~CLexerForCursedWeapon() { m_FileBuf = nullptr; }

int CLexerForCursedWeapon::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForCursedWeapon::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
    auto* const semanticInt = reinterpret_cast<std::int32_t*>(semanticSlot);
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "/*" .* "*/" { EndToken(); continue; }
            "cursed_weapon_begin" { EndToken(); return TK_CURSED_WEAPON_BEGIN; }
            "cursed_weapon_end" { EndToken(); return TK_CURSED_WEAPON_END; }
            "item_name" { EndToken(); return TK_ITEM_NAME; }
            "create_rate_per_10min" { EndToken(); return TK_CREATE_RATE_PER_10MIN; }
            "create_rate_per_npc" { EndToken(); return TK_CREATE_RATE_PER_NPC; }
            "drop_rate_ondying" { EndToken(); return TK_DROP_RATE_ONDYING; }
            "life_time" { EndToken(); return TK_LIFE_TIME; }
            "expire_by_nonpk" { EndToken(); return TK_EXPIRE_BY_NONPK; }
            "expire_by_drop" { EndToken(); return TK_EXPIRE_BY_DROP; }
            "transform_id" { EndToken(); return TK_TRANSFORM_ID; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticInt = ParseWindowsInt(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticInt = ParseWindowsInt(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)));
                return TK_INTEGER;
            }
            [&;={}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const char16_t* const itemName = yytext + 1;
                const std::int64_t value = g_PrecompiledHeaderDB.Search(itemName);
                if (value == -1) {
                    // const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostName.c_str())
                    L2_LOG_ERROR(
                        Log, "Undefined precompiled-header name '%s' at script line %lld; passing -1 to the parser.",
                        Utf16ToUtf8(std::u16string_view(itemName)).c_str(), static_cast<long long>(line) + 1
                    );
                }
                *semanticInt = static_cast<std::int32_t>(value);
                return TK_INTEGER;
            }
            [\t\r _] { EndToken(); continue; }
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
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
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

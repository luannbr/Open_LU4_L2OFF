// AI object scanner (original L2NPC/loader_lex.l).
// re2c generates LoaderLex.cpp.

#include "L2NPC/src/LoaderLex.h"

#include "L2NPC/src/LoaderParser.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "LoaderParserTokens.h"

namespace {

std::string AsciiText(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char16_t codeUnit : value) {
        result.push_back(codeUnit <= 0x7f ? static_cast<char>(codeUnit) : '?');
    }
    return result;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = AsciiText(value);
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (errno == ERANGE) {
        return parsed < 0 ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(parsed);
}

float ParseWindowsFloat(std::u16string_view value) {
    const std::string ascii = AsciiText(value);
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0F : static_cast<float>(parsed);
}

char16_t* DuplicateUtf16(std::u16string_view value) {
    auto* const copy = static_cast<char16_t*>(std::malloc((value.size() + 1U) * sizeof(char16_t)));
    if (!copy) {
        return nullptr;
    }
    if (!value.empty()) {
        std::memcpy(copy, value.data(), value.size() * sizeof(char16_t));
    }
    copy[value.size()] = u'\0';
    return copy;
}

} // namespace

CLexerForLoader::CLexerForLoader() {
    // Current constructor 0x00549f7c.
    line = 0;
    mWordSize = 0;
    m_bFirstTime = true;
}

CLexerForLoader::~CLexerForLoader() = default;

int CLexerForLoader::create(char16_t* fileName) {
    // Current 0x00549ee4 uses LoadBinaryAbsolute and keeps that image for
    // process lifetime. Semantic strings are separately allocated by the
    // generated lexer actions below.
    std::wstring hostPath = Utf16ToWide(std::u16string_view(fileName));
#if !defined(_WIN32)
    // The target loader receives and keeps Windows path spelling. Only
    // the portable filesystem boundary translates its separators.
    for (wchar_t& character : hostPath) {
        if (character == L'\\') {
            character = L'/';
        }
    }
#endif
    m_FileBuf = LoadBinaryAbsolute(hostPath.c_str(), m_nFileSize, false);
    if (!m_FileBuf) {
        return 0;
    }
    return 1;
}

int CLexerForLoader::yygetchar() {
    // Current 0x00549f30: the first two bytes are skipped as the UTF-16LE
    // BOM, then source words are assembled little-endian.
    if (m_bFirstTime) {
        m_nFilePointer = 2;
        m_bFirstTime = false;
    }
    if (m_nFilePointer + 1 >= m_nFileSize) {
        return -1;
    }
    const auto* const bytes = reinterpret_cast<const unsigned char*>(m_FileBuf);
    const unsigned value =
        static_cast<unsigned>(bytes[m_nFilePointer]) | (static_cast<unsigned>(bytes[m_nFilePointer + 1]) << 8U);
    m_nFilePointer += 2;
    return static_cast<int>(value);
}

int CLexerForLoader::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    std::uint64_t& semantic = *lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "SizeofPointer" { EndToken(); return TK_SIZEOFPOINTER; }
            "SharedFactoryVersion" { EndToken(); return TK_SHAREDFACTORYVERSION; }
            "NPCHVersion" { EndToken(); return TK_NPCHVERSION; }
            "NASCVersion" { EndToken(); return TK_NASCVERSION; }
            "NPCEventHVersion" { EndToken(); return TK_NPCEVENTHVERSION; }
            "Debug" { EndToken(); return TK_DEBUG_KW; }
            "class" { EndToken(); return TK_CLASS; }
            "ts_class" { EndToken(); return TK_TS_CLASS; }
            "bc_class" { EndToken(); return TK_BC_CLASS; }
            "dev_class" { EndToken(); return TK_DEV_CLASS; }
            "class_end" { EndToken(); return TK_CLASS_END; }
            "handler" { EndToken(); return TK_HANDLER; }
            "handler_end" { EndToken(); return TK_HANDLER_END; }
            "parameter_define_begin" { EndToken(); return TK_PARAMETER_DEFINE_BEGIN; }
            "parameter_define_end" { EndToken(); return TK_PARAMETER_DEFINE_END; }
            "property_define_begin" { EndToken(); return TK_PROPERTY_DEFINE_BEGIN; }
            "property_define_end" { EndToken(); return TK_PROPERTY_DEFINE_END; }
            "telposlist_begin" { EndToken(); return TK_TELPOSLIST_BEGIN; }
            "telposlist_end" { EndToken(); return TK_TELPOSLIST_END; }
            "buyselllist_begin" { EndToken(); return TK_BUYSELLLIST_BEGIN; }
            "buyselllist_end" { EndToken(); return TK_BUYSELLLIST_END; }
            "waypointstype" { EndToken(); return TK_WAYPOINTSTYPE; }
            "waypointdelaystype" { EndToken(); return TK_WAYPOINTDELAYSTYPE; }
            "variable_begin" { EndToken(); return TK_VARIABLE_BEGIN; }
            "variable_end" { EndToken(); return TK_VARIABLE_END; }
            "int" { EndToken(); return TK_INT_KW; }
            "float" { EndToken(); return TK_FLOAT_KW; }
            "string" { EndToken(); return TK_STRING; }
            // Opcode tokens store only the low four bytes of the value, as the
            // original actions did.
            "push_event" { EndToken(); grammar::SetLow32(semantic, 0); return TK_OPCODE; }
            "add" { EndToken(); grammar::SetLow32(semantic, 1); return TK_OPCODE; }
            "sub" { EndToken(); grammar::SetLow32(semantic, 2); return TK_OPCODE; }
            "mul" { EndToken(); grammar::SetLow32(semantic, 3); return TK_OPCODE; }
            "div" { EndToken(); grammar::SetLow32(semantic, 4); return TK_OPCODE; }
            "mod" { EndToken(); grammar::SetLow32(semantic, 5); return TK_OPCODE; }
            "fetch_i" { EndToken(); grammar::SetLow32(semantic, 6); return TK_OPCODE; }
            "fetch_i4" { EndToken(); grammar::SetLow32(semantic, 7); return TK_OPCODE; }
            "fetch_f" { EndToken(); grammar::SetLow32(semantic, 8); return TK_OPCODE; }
            "fetch_d" { EndToken(); grammar::SetLow32(semantic, 9); return TK_OPCODE; }
            "equal" { EndToken(); grammar::SetLow32(semantic, 10); return TK_OPCODE; }
            "less" { EndToken(); grammar::SetLow32(semantic, 12); return TK_OPCODE; }
            "less_equal" { EndToken(); grammar::SetLow32(semantic, 13); return TK_OPCODE; }
            "greater" { EndToken(); grammar::SetLow32(semantic, 15); return TK_OPCODE; }
            "greater_equal" { EndToken(); grammar::SetLow32(semantic, 14); return TK_OPCODE; }
            "negate" { EndToken(); grammar::SetLow32(semantic, 16); return TK_OPCODE; }
            "push_reg_sp" { EndToken(); grammar::SetLow32(semantic, 33); return TK_OPCODE; }
            "not_equal" { EndToken(); grammar::SetLow32(semantic, 11); return TK_OPCODE; }
            "and" { EndToken(); grammar::SetLow32(semantic, 18); return TK_OPCODE; }
            "or" { EndToken(); grammar::SetLow32(semantic, 19); return TK_OPCODE; }
            "not" { EndToken(); grammar::SetLow32(semantic, 17); return TK_OPCODE; }
            "assign" { EndToken(); grammar::SetLow32(semantic, 20); return TK_OPCODE; }
            "assign4" { EndToken(); grammar::SetLow32(semantic, 21); return TK_OPCODE; }
            "exit_handler" { EndToken(); grammar::SetLow32(semantic, 22); return TK_OPCODE; }
            "call_super" { EndToken(); grammar::SetLow32(semantic, 23); return TK_OPCODE; }
            "bit_and" { EndToken(); grammar::SetLow32(semantic, 24); return TK_OPCODE; }
            "bit_or" { EndToken(); grammar::SetLow32(semantic, 25); return TK_OPCODE; }
            "xor" { EndToken(); grammar::SetLow32(semantic, 26); return TK_OPCODE; }
            "push_const" { EndToken(); grammar::SetLow32(semantic, 29); return TK_OPCODE_WITH_OPERAND; }
            "push_string" { EndToken(); grammar::SetLow32(semantic, 30); return TK_OPCODE_WITH_OPERAND; }
            "push_parameter" { EndToken(); grammar::SetLow32(semantic, 31); return TK_OPCODE_WITH_OPERAND; }
            "push_property" { EndToken(); grammar::SetLow32(semantic, 32); return TK_OPCODE_WITH_OPERAND; }
            "shift_sp" { EndToken(); grammar::SetLow32(semantic, 34); return TK_OPCODE_WITH_OPERAND; }
            "func_call" { EndToken(); grammar::SetLow32(semantic, 35); return TK_OPCODE_WITH_OPERAND; }
            "jump" { EndToken(); grammar::SetLow32(semantic, 36); return TK_OPCODE_WITH_OPERAND; }
            "branch_false" { EndToken(); grammar::SetLow32(semantic, 27); return TK_OPCODE_WITH_OPERAND; }
            "branch_true" { EndToken(); grammar::SetLow32(semantic, 28); return TK_OPCODE_WITH_OPERAND; }
            "add_string" { EndToken(); grammar::SetLow32(semantic, 37); return TK_OPCODE_WITH_OPERAND; }
            "(null)" { EndToken(); return TK_NULL_NAME; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                const std::int64_t value = ParseWindowsInt64(match);
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_INTEGER;
            }
            "S" ("0" | [1-9][0-9]*) {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                const std::int64_t value = ParseWindowsInt64(match.substr(1));
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_STRING_INDEX;
            }
            "L" ("0" | [1-9][0-9]*) {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                const std::int64_t value = ParseWindowsInt64(match.substr(1));
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_LABEL;
            }
            "-"? "-"? ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                const float value = ParseWindowsFloat(match);
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                char16_t* const value = DuplicateUtf16(match);
                semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));
                mWordSize = runtime::Int32FromBits(
                    static_cast<std::uint32_t>(mWordSize) + static_cast<std::uint32_t>(match.size()) * 2U
                );
                return TK_IDENTIFIER;
            }
            [.:;={}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "\"" [^"]* "\"" {
                EndToken();
                const std::uint32_t byteCountBits = (static_cast<std::uint32_t>(yyleng) - 1U) * 2U;
                std::int32_t byteCount = 0;
                std::memcpy(&byteCount, &byteCountBits, sizeof(byteCount));
                auto* const copy =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(static_cast<std::int64_t>(byteCount))));
                if (copy) {
                    const std::size_t copyUnits = static_cast<std::size_t>(static_cast<std::int64_t>(yyleng) - 2);
                    std::memcpy(copy, yytext + 1, copyUnits * sizeof(char16_t));
                    copy[static_cast<std::int64_t>(yyleng) - 2] = u'\0';
                    semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(copy));
                    mWordSize = runtime::Int32FromBits(static_cast<std::uint32_t>(mWordSize) + byteCountBits);
                }
                return TK_STRING_LITERAL;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                line = runtime::AddInt32Wrapping(line, 1);
                continue;
            }
            . {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                // std::printf("invalid character\n")
                // Log.Add(LOG_ERROR,
                //     "[AI load] Invalid UTF-16 code unit 0x%04X at lexer line %d. Continuing tokenization.",
                //     match.empty() ? 0U : static_cast<unsigned int>(match.front()), line)
                // L2_LOG_ERROR(Log,
                //     "[AI load] Invalid UTF-16 code unit 0x%04X at lexer line %d. Continuing tokenization.",
                //     match.empty() ? 0U : static_cast<unsigned int>(match.front()), line)
                L2_LOG_ERROR(
                    Log, "[AI load] Invalid UTF-16 code unit 0x%04X at line %lld; continuing tokenization.",
                    match.empty() ? 0U : static_cast<unsigned int>(match.front()), static_cast<long long>(line) + 1
                );
                continue;
            }
            $ { return 0; }
        */
    }
}

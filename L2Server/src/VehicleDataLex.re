// CLexerForVehicle scanner (original L2Server/vehicledata_lex.l).
// re2c generates VehicleDataLex.cpp.

#include "L2Server/src/VehicleDataLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/VehicleDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#include "VehicleDataParserTokens.h"

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
        return parsed < 0 ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
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

CLexerForVehicle::CLexerForVehicle() : m_pFileBuf(nullptr), line(0), comment_count(0) {
}

CLexerForVehicle::~CLexerForVehicle() { std::free(m_pFileBuf); }

int CLexerForVehicle::Create(const wchar_t* fileName) {
    m_pFileBuf = LoadBinaryAbsoluteSecure(fileName, m_nFileSize, true);
    if (m_pFileBuf == nullptr) {
        return 0;
    }
    m_nFilePointer = 2;
    return 1;
}

int CLexerForVehicle::yygetchar() {
    if (m_nFileSize <= m_nFilePointer + 1) {
        return -1;
    }
    const auto value = *reinterpret_cast<const std::uint16_t*>(m_pFileBuf + m_nFilePointer);
    m_nFilePointer += 2;
    return static_cast<int>(value);
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForVehicle::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
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
            <MAIN> "vehicle_begin" { EndToken(); return TK_VEHICLE_BEGIN; }
            <MAIN> "vehicle_end" { EndToken(); return TK_VEHICLE_END; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "captain" { EndToken(); return TK_CAPTAIN; }
            <MAIN> "route_begin" { EndToken(); return TK_ROUTE_BEGIN; }
            <MAIN> "route_end" { EndToken(); return TK_ROUTE_END; }
            <MAIN> "station_begin" { EndToken(); return TK_STATION_BEGIN; }
            <MAIN> "station_end" { EndToken(); return TK_STATION_END; }
            <MAIN> "action_begin" { EndToken(); return TK_ACTION_BEGIN; }
            <MAIN> "action_end" { EndToken(); return TK_ACTION_END; }
            <MAIN> "message" { EndToken(); return TK_MESSAGE; }
            <MAIN> "departure" { EndToken(); return TK_DEPARTURE; }
            <MAIN> "check_ticket" { EndToken(); return TK_CHECK_TICKET; }
            <MAIN> "set_kick_pos" { EndToken(); return TK_SET_KICK_POS; }
            <MAIN> "play_sound" { EndToken(); return TK_PLAY_SOUND; }
            <MAIN> "wait" { EndToken(); return TK_WAIT; }
            <MAIN> "point" { EndToken(); return TK_POINT; }
            <MAIN> "station_id" { EndToken(); return TK_STATION_ID; }
            <MAIN> "broadcast_point" { EndToken(); return TK_BROADCAST_POINT; }
            <MAIN> "none" { EndToken(); return TK_NONE; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
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
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME_2;
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
                //     static_cast<wchar_t>(*yytext))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
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

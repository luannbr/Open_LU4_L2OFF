// CLexerForAirShip scanner (original L2Server/lexer_airship.l).
// re2c generates LexerAirShip.cpp.

#include "L2Server/src/LexerAirShip.h"

#include "L2Server/src/ParserAirShip.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "ParserAirShipTokens.h"

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

CLexerForAirShip::CLexerForAirShip() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForAirShip::~CLexerForAirShip() { m_FileBuf = nullptr; }

int CLexerForAirShip::Create(char16_t* buffer, std::int32_t size) {
    m_nFilePointer = 0;
    m_FileBuf = buffer;
    m_nFileSize = size;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {
    yycINITIAL, yycCOMMENT, yycMAIN, yycMAIN2, yycMAIN3, yycMAIN4, yycMAIN5, yycMAIN6, yycMAIN7
};

} // namespace

int CLexerForAirShip::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "airport_begin" { EndToken(); return TK_AIRPORT_BEGIN; }
            <MAIN> "airport_end" { EndToken(); return TK_AIRPORT_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "airport_type" {
                EndToken();
                yybegin(8);
                return TK_AIRPORT_TYPE;
            }
            <MAIN> "airship_pos" { EndToken(); return TK_AIRSHIP_POS; }
            <MAIN> "getoff_pos" { EndToken(); return TK_GETOFF_POS; }
            <MAIN> "stopover" { EndToken(); return TK_STOPOVER; }
            <MAIN> "teleport_begin" { EndToken(); return TK_TELEPORT_BEGIN; }
            <MAIN> "teleport_end" { EndToken(); return TK_TELEPORT_END; }
            <MAIN> "expel" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(-1));
                return TK_EXPEL_GROUP;
            }
            <MAIN> "fuel" { EndToken(); return TK_FUEL; }
            <MAIN> "tel_pos" { EndToken(); return TK_TEL_POS; }
            <MAIN> "platform_begin" { EndToken(); return TK_PLATFORM_BEGIN; }
            <MAIN> "platform_end" { EndToken(); return TK_PLATFORM_END; }
            <MAIN> "landing_path" { EndToken(); return TK_LANDING_PATH; }
            <MAIN> "takeoff_path" { EndToken(); return TK_TAKEOFF_PATH; }
            <MAIN3> "REGULAR" {
                EndToken();
                yybegin(4);
                return TK_REGULAR;
            }
            <MAIN3> "FREE" {
                EndToken();
                yybegin(4);
                return TK_FREE;
            }
            <MAIN> "airship_begin" { EndToken(); return TK_AIRSHIP_BEGIN; }
            <MAIN> "airship_end" { EndToken(); return TK_AIRSHIP_END; }
            <MAIN> "airship_type" {
                EndToken();
                yybegin(6);
                return TK_AIRSHIP_TYPE;
            }
            <MAIN> "speed" { EndToken(); return TK_SPEED; }
            <MAIN> "collision" { EndToken(); return TK_COLLISION; }
            <MAIN> "max_passenger" { EndToken(); return TK_MAX_PASSENGER; }
            <MAIN> "start_pos" { EndToken(); return TK_START_POS; }
            <MAIN> "path" { EndToken(); return TK_PATH; }
            <MAIN> "return_to_start" {
                EndToken();
                yybegin(10);
                return TK_RETURN_TO_START;
            }
            <MAIN> "move" { EndToken(); return TK_MOVE; }
            <MAIN> "tel" { EndToken(); return TK_TEL; }
            <MAIN> "airport" { EndToken(); return TK_AIRPORT; }
            <MAIN> "steering_begin" { EndToken(); return TK_STEERING_BEGIN; }
            <MAIN> "steering_end" { EndToken(); return TK_STEERING_END; }
            <MAIN> "steering_pos" { EndToken(); return TK_STEERING_POS; }
            <MAIN> "driver_pos" { EndToken(); return TK_DRIVER_POS; }
            <MAIN> "steering_item" { EndToken(); return TK_STEERING_ITEM; }
            <MAIN> "fuel_max" { EndToken(); return TK_FUEL_MAX; }
            <MAIN> "fuel_consume" { EndToken(); return TK_FUEL_CONSUME; }
            <MAIN> "buy_cost" { EndToken(); return TK_BUY_COST; }
            <MAIN> "summon_cost" { EndToken(); return TK_SUMMON_COST; }
            <MAIN2> "REGULAR" {
                EndToken();
                yybegin(4);
                return TK_REGULAR_2;
            }
            <MAIN2> "PLEDGE" {
                EndToken();
                yybegin(4);
                *lval = 1;
                return TK_EXPEL_GROUP;
            }
            <MAIN2> "GROUP" {
                EndToken();
                yybegin(4);
                *lval = 3;
                return TK_EXPEL_GROUP;
            }
            <MAIN2> "INDIVIDUAL" {
                EndToken();
                yybegin(4);
                *lval = 2;
                return TK_EXPEL_GROUP;
            }
            <MAIN4> "move" {
                EndToken();
                yybegin(4);
                *lval = 2;
                return TK_EXPEL_GROUP;
            }
            <MAIN4> "tel" {
                EndToken();
                yybegin(4);
                *lval = 3;
                return TK_EXPEL_GROUP;
            }
            <MAIN> "airship_area_begin" { EndToken(); return TK_AIRSHIP_AREA_BEGIN; }
            <MAIN> "airship_area_end" { EndToken(); return TK_AIRSHIP_AREA_END; }
            <MAIN> "map_no" { EndToken(); return TK_MAP_NO; }
            <MAIN> "area_type" {
                EndToken();
                yybegin(12);
                return TK_AREA_TYPE;
            }
            <MAIN5> "AIRPORT" {
                EndToken();
                yybegin(4);
                *lval = 0;
                return TK_EXPEL_GROUP;
            }
            <MAIN5> "SPEED" {
                EndToken();
                yybegin(4);
                *lval = 1;
                return TK_EXPEL_GROUP;
            }
            <MAIN> "area_range" { EndToken(); return TK_AREA_RANGE; }
            <INITIAL,COMMENT,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_EXPEL_GROUP;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <INITIAL,COMMENT,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <INITIAL,COMMENT,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <INITIAL,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> [%,:;=[\]{}~] {
                EndToken();
                return grammar::CharacterToken(yytext[0]);
            }
            <INITIAL,COMMENT,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <INITIAL,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> . { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN,MAIN2,MAIN3,MAIN4,MAIN5,MAIN6,MAIN7> $ { return 0; }
        */
    }
}

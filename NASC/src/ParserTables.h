#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nasc {

// The table layouts match NASC64_2015.exe on x64.
struct YyLexerTransition {
    std::int16_t next;
    std::int16_t check;
};

struct YyLexerState {
    std::int16_t def;
    std::int32_t base;
    std::int16_t match;
};

struct YyReduction {
    std::int16_t nonterminal;
    std::int16_t length;
    std::int16_t action;
};

struct YyTokenAction {
    std::int16_t check;
    std::uint8_t type;
    std::int16_t shiftOrReduce;
};

struct YyStateAction {
    std::int32_t base;
    std::uint8_t lookahead;
    std::uint8_t type;
    std::int16_t shiftOrReduce;
};

struct YyNonterminalGoto {
    std::int16_t check;
    std::int16_t next;
};

struct YyStateGoto {
    std::int16_t base;
    std::int16_t defaultState;
};

static_assert(sizeof(YyLexerTransition) == 4);
static_assert(sizeof(YyLexerState) == 12);
static_assert(offsetof(YyLexerState, base) == 4);
static_assert(offsetof(YyLexerState, match) == 8);
static_assert(sizeof(YyReduction) == 6);
static_assert(sizeof(YyTokenAction) == 6);
static_assert(offsetof(YyTokenAction, shiftOrReduce) == 4);
static_assert(sizeof(YyStateAction) == 8);
static_assert(sizeof(YyNonterminalGoto) == 4);
static_assert(sizeof(YyStateGoto) == 4);

extern const std::array<std::int16_t, 6> lexerMatch;
extern const std::array<YyLexerTransition, 464> lexerTransitions;
extern const std::array<YyLexerState, 229> lexerStates;
extern const std::array<std::uint8_t, 55> lexerBackup;

extern const std::array<YyReduction, 159> parserReductions;
extern const std::array<YyTokenAction, 321> parserTokenActions;
extern const std::array<YyStateAction, 332> parserStateActions;
extern const std::array<YyNonterminalGoto, 131> parserNonterminalGotos;
extern const std::array<YyStateGoto, 332> parserStateGotos;

struct TableEvidence {
    std::string_view name;
    std::uint64_t virtualAddress;
    std::size_t byteCount;
    std::string_view sha256;
};

extern const std::array<TableEvidence, 9> tableEvidence;

} // namespace nasc

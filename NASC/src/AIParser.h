#pragma once

#include "AILexer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nasc {

class AISemanticActions;

enum class ParserActionKind : std::uint8_t {
    Shift = 0,
    Reduce = 1,
    Error = 2,
    Accept = 3,
    Fallback = 4,
};

struct ParserTraceEntry {
    ParserActionKind kind = ParserActionKind::Error;
    std::int16_t state = 0;
    std::int16_t argument = 0;
    int lookahead = 0;
};

struct ParserDiagnostic {
    std::size_t line = 1;
    int token = 0;
    std::int16_t state = 0;
    std::u16string message = u"syntax error";
};

struct AIParseResult {
    bool accepted = false;
    std::size_t shiftedTokens = 0;
    std::size_t reductions = 0;
    std::size_t consumedTokens = 0;
    std::size_t semanticActions = 0;
    std::size_t recoveryShifts = 0;
    std::size_t discardedTokens = 0;
    std::vector<ParserDiagnostic> diagnostics;
    // The first syntax error, in the short form of the compiler report.
    std::optional<AIToken> errorToken;
    std::int16_t errorState = 0;
    std::vector<ParserTraceEntry> trace;
};

// Runs the LALR tables of NASC64_2015.exe with the original error recovery:
// the error token 0x10000, three shifts before the next error report and
// dropped lookahead tokens. The overload with AISemanticActions also runs
// the grammar actions.
class AISyntaxParser {
public:
    explicit AISyntaxParser(bool recordTrace = false) noexcept;

    [[nodiscard]] AIParseResult Parse(AILexer& lexer) const;
    [[nodiscard]] AIParseResult Parse(AILexer& lexer, AISemanticActions& semanticActions) const;

private:
    struct Action {
        ParserActionKind kind;
        std::int16_t argument;
        std::int16_t tableState;
    };

    [[nodiscard]] static Action ResolveAction(std::int16_t state, int lookahead);
    [[nodiscard]] static bool ActionUsesLookahead(std::int16_t state);
    [[nodiscard]] static std::int16_t ResolveGoto(std::int16_t state, std::int16_t nonterminal);
    [[nodiscard]] AIParseResult ParseImpl(AILexer& lexer, AISemanticActions* semanticActions) const;

    bool recordTrace_ = false;
};

} // namespace nasc

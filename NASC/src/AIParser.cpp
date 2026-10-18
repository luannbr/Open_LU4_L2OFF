#include "AIParser.h"

#include "ParserTables.h"
#include "SemanticActions.h"

#include <bit>
#include <limits>
#include <stdexcept>

namespace nasc {
namespace {

constexpr std::size_t parserStackInitialCapacity = 100;
constexpr int parserErrorToken = 0x10000;
constexpr int parserRecoveryShiftCount = 3;

[[nodiscard]] ParserActionKind ActionKind(std::uint8_t value) {
    if (value > static_cast<std::uint8_t>(ParserActionKind::Fallback)) {
        throw std::logic_error("parser action type is outside 0..4");
    }
    return static_cast<ParserActionKind>(value);
}

[[nodiscard]] std::int16_t CheckedState(std::int16_t state) {
    if (state < 0 || static_cast<std::size_t>(state) >= parserStateActions.size()) {
        throw std::logic_error("parser state is outside the exact table");
    }
    return state;
}

[[nodiscard]] AISemanticValue TokenValue(const AIToken& token) {
    if (token.code == 0x10001 && token.integerValue) {
        return AISemanticValue::FromInteger(std::bit_cast<std::int64_t>(*token.integerValue));
    }
    if (token.code == 0x10002 && token.floatingValue) {
        return AISemanticValue::FromFloat(*token.floatingValue);
    }
    return AISemanticValue::FromText(token.lexeme);
}

} // namespace

AISyntaxParser::AISyntaxParser(bool recordTrace) noexcept : recordTrace_(recordTrace) {}

AISyntaxParser::Action AISyntaxParser::ResolveAction(std::int16_t state, int lookahead) {
    std::int16_t tableState = CheckedState(state);
    for (;;) {
        const YyStateAction& stateAction = parserStateActions[static_cast<std::size_t>(tableState)];
        ParserActionKind kind = ActionKind(stateAction.type);
        std::int16_t argument = stateAction.shiftOrReduce;

        if (stateAction.lookahead != 0) {
            const std::int64_t candidate = static_cast<std::int64_t>(stateAction.base) + lookahead;
            if (candidate >= 0 && static_cast<std::size_t>(candidate) < parserTokenActions.size()) {
                const YyTokenAction& tokenAction = parserTokenActions[static_cast<std::size_t>(candidate)];
                if (tokenAction.check == tableState) {
                    kind = ActionKind(tokenAction.type);
                    argument = tokenAction.shiftOrReduce;
                }
            }
        }

        if (kind != ParserActionKind::Fallback) {
            return Action{kind, argument, tableState};
        }
        tableState = CheckedState(argument);
    }
}

bool AISyntaxParser::ActionUsesLookahead(std::int16_t state) {
    std::int16_t tableState = CheckedState(state);
    for (;;) {
        const YyStateAction& stateAction = parserStateActions[static_cast<std::size_t>(tableState)];
        if (stateAction.lookahead != 0) {
            return true;
        }
        if (ActionKind(stateAction.type) != ParserActionKind::Fallback) {
            return false;
        }
        tableState = CheckedState(stateAction.shiftOrReduce);
    }
}

std::int16_t AISyntaxParser::ResolveGoto(std::int16_t state, std::int16_t nonterminal) {
    std::int16_t tableState = CheckedState(state);
    for (;;) {
        const YyStateGoto& stateGoto = parserStateGotos[static_cast<std::size_t>(tableState)];
        const std::int32_t candidate = static_cast<std::int32_t>(stateGoto.base) + nonterminal;
        if (candidate >= 0 && static_cast<std::size_t>(candidate) < parserNonterminalGotos.size()) {
            const YyNonterminalGoto& nonterminalGoto = parserNonterminalGotos[static_cast<std::size_t>(candidate)];
            if (nonterminalGoto.check == tableState) {
                return CheckedState(nonterminalGoto.next);
            }
        }
        if (stateGoto.defaultState == -1) {
            throw std::logic_error("parser has no goto for a reduction");
        }
        tableState = CheckedState(stateGoto.defaultState);
    }
}

AIParseResult AISyntaxParser::Parse(AILexer& lexer) const { return ParseImpl(lexer, nullptr); }

AIParseResult AISyntaxParser::Parse(AILexer& lexer, AISemanticActions& semanticActions) const {
    return ParseImpl(lexer, &semanticActions);
}

AIParseResult AISyntaxParser::ParseImpl(AILexer& lexer, AISemanticActions* semanticActions) const {
    AIParseResult result;
    std::vector<std::int16_t> stack;
    std::vector<AISemanticValue> semanticStack;
    stack.reserve(parserStackInitialCapacity);
    semanticStack.reserve(parserStackInitialCapacity);
    stack.push_back(0); // Start state

    std::optional<AIToken> lookahead;
    int recoveryShiftsRemaining = 0; // Shifts left before the next syntax error is reported
    for (;;) {
        const std::int16_t state = stack.back();
        // Read a token only when the state needs a lookahead, like the original.
        // This decides the line number of diagnostics from reductions.
        if (!lookahead && ActionUsesLookahead(state)) {
            lookahead = lexer.NextToken();
            if (lookahead->code < 0) {
                lookahead->code = 0;
            }
        }

        const Action action = ResolveAction(state, lookahead ? lookahead->code : 0);
        if (recordTrace_) {
            result.trace.push_back(ParserTraceEntry{action.kind, state, action.argument, lookahead ? lookahead->code : -1});
        }

        switch (action.kind) {
            case ParserActionKind::Shift:
                if (!lookahead) {
                    throw std::logic_error("parser shift has no generated lookahead value");
                }
                if (recoveryShiftsRemaining > 0) {
                    --recoveryShiftsRemaining;
                }
                stack.push_back(CheckedState(action.argument));
                semanticStack.push_back(TokenValue(*lookahead));
                ++result.shiftedTokens;
                ++result.consumedTokens;
                lookahead.reset();
                break;

            case ParserActionKind::Reduce: {
                if (action.argument < 0 || static_cast<std::size_t>(action.argument) >= parserReductions.size()) {
                    throw std::logic_error("parser reduction is outside the exact table");
                }
                const YyReduction& reduction = parserReductions[static_cast<std::size_t>(action.argument)];
                if (reduction.length < 0 || static_cast<std::size_t>(reduction.length) >= stack.size()) {
                    throw std::logic_error("parser reduction underflows its state stack");
                }
                const std::size_t length = static_cast<std::size_t>(reduction.length);
                if (length > semanticStack.size()) {
                    throw std::logic_error("parser reduction underflows its semantic stack");
                }
                AISemanticValue reducedValue;
                if (reduction.action >= 0 && semanticActions != nullptr) {
                    semanticActions->SetSourceLine(lookahead ? lookahead->line : lexer.SourceLine());
                    reducedValue = semanticActions->Execute(reduction.action, semanticStack, reduction.length);
                    ++result.semanticActions;
                } else if (length != 0) {
                    // Like the original parser, $$ starts as $1, also for a rule without an
                    // action.
                    reducedValue = semanticStack[semanticStack.size() - length];
                }
                stack.resize(stack.size() - static_cast<std::size_t>(reduction.length));
                semanticStack.resize(semanticStack.size() - length);
                stack.push_back(ResolveGoto(stack.back(), reduction.nonterminal));
                semanticStack.push_back(std::move(reducedValue));
                if (stack.size() != semanticStack.size() + 1U) {
                    throw std::logic_error("parser stacks lost synchronization");
                }
                ++result.reductions;
                break;
            }

            case ParserActionKind::Accept:
                result.accepted = true;
                return result;

            case ParserActionKind::Error:
                if (recoveryShiftsRemaining == 0) {
                    const std::size_t errorLine = lookahead ? lookahead->line : lexer.SourceLine();
                    const int errorCode = lookahead ? lookahead->code : -1;
                    result.diagnostics.push_back(ParserDiagnostic{errorLine, errorCode, state, u"syntax error"});
                    if (!result.errorToken) {
                        result.errorToken = lookahead ? *lookahead : AIToken{-1, 0, {}, errorLine, {}, {}, false};
                        result.errorState = state;
                    }
                }

                if (recoveryShiftsRemaining < parserRecoveryShiftCount) {
                    // Pop states until one can shift the error token. The tables have no
                    // destructors, so a pop only removes the state and its value.
                    bool foundRecoveryShift = false;
                    while (!stack.empty()) {
                        const std::int16_t recoveryState = stack.back();
                        const Action recoveryAction = ResolveAction(recoveryState, parserErrorToken);
                        if (recoveryAction.kind == ParserActionKind::Shift) {
                            if (recordTrace_) {
                                result.trace.push_back(
                                    ParserTraceEntry{
                                        recoveryAction.kind, recoveryState, recoveryAction.argument, parserErrorToken}
                                );
                            }
                            stack.push_back(CheckedState(recoveryAction.argument));
                            // The error token has no value. An empty slot keeps the value
                            // stack aligned with the state stack.
                            semanticStack.emplace_back();
                            recoveryShiftsRemaining = parserRecoveryShiftCount;
                            ++result.recoveryShifts;
                            foundRecoveryShift = true;
                            break;
                        }

                        stack.pop_back();
                        if (!semanticStack.empty()) {
                            semanticStack.pop_back();
                        }
                    }
                    if (!foundRecoveryShift) {
                        return result;
                    }
                    break;
                }

                // A second error within three shifts drops the current token.
                // At the end of input the parse stops, like the original.
                if (lookahead) {
                    if (lookahead->code == 0) {
                        return result;
                    }
                    ++result.discardedTokens;
                    ++result.consumedTokens;
                    lookahead.reset();
                }
                break;

            case ParserActionKind::Fallback:
                throw std::logic_error("parser exposed an unresolved fallback");
        }
    }
}

} // namespace nasc

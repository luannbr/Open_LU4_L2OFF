#include "AILexer.h"

#include "AIDefinitions.h"
#include "ParserTables.h"
#include "PrecompiledHeaderDB.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nasc {
namespace {

constexpr int endOfInput = -1;

std::uint64_t ParseUnsigned(std::u16string_view text, int base) {
    std::string ascii;
    ascii.reserve(text.size());
    for (char16_t character : text) {
        if (character > 0x7f) {
            return 0;
        }
        ascii.push_back(static_cast<char>(character));
    }
    std::uint64_t value = 0;
    const auto result = std::from_chars(ascii.data(), ascii.data() + ascii.size(), value, base);
    if (result.ec == std::errc::result_out_of_range) {
        // Like _wcstoui64, the conversion saturates on overflow. The lexer rules
        // already check the spelling, so any other failure gives 0, as in the
        // original.
        return std::numeric_limits<std::uint64_t>::max();
    }
    if (result.ec != std::errc{} || result.ptr != ascii.data() + ascii.size()) {
        return 0;
    }
    return value;
}

std::optional<float> ParseFloat(std::u16string_view text) {
    std::string ascii;
    ascii.reserve(text.size());
    for (char16_t character : text) {
        if (character > 0x7f) {
            return std::nullopt;
        }
        ascii.push_back(static_cast<char>(character));
    }
    char* end = nullptr;
    // The original reads the value with wcstod and then stores it as float.
    // Keep this two-step rounding.
    const double parsed = std::strtod(ascii.c_str(), &end);
    if (end != ascii.c_str() + ascii.size()) {
        return std::nullopt;
    }
    return static_cast<float>(parsed);
}

} // namespace

RuleLexer::RuleLexer(std::u16string_view input) : input_(input) {}

void RuleLexer::SetStartCondition(int condition) noexcept {
    startCondition_ = condition;
    eofActionChecked_ = false;
}

int RuleLexer::StartCondition() const noexcept { return startCondition_; }

std::size_t RuleLexer::InputPosition() const noexcept { return cursor_; }

std::size_t RuleLexer::EngineLine() const noexcept { return engineLine_; }

int RuleLexer::ReadCharacter() noexcept {
    if (cursor_ == input_.size()) {
        return endOfInput;
    }
    const char16_t character = input_[cursor_++];
    if (character == u'\n') {
        ++engineLine_;
    }
    return static_cast<int>(character);
}

void RuleLexer::UnreadCharacter(char16_t character) {
    if (cursor_ == 0 || input_[cursor_ - 1] != character) {
        throw std::logic_error("lexer unput invariant failed");
    }
    --cursor_;
    if (character == u'\n') {
        --engineLine_;
    }
}

int RuleLexer::Transition(int state, int character) const {
    while (true) {
        if (state < 0 || static_cast<std::size_t>(state) >= lexerStates.size()) {
            throw std::logic_error("lexer state is outside the exact table");
        }
        const YyLexerState& descriptor = lexerStates[static_cast<std::size_t>(state)];
        const std::int64_t candidate = static_cast<std::int64_t>(descriptor.base) + character;
        if (candidate >= 0 && static_cast<std::size_t>(candidate) < lexerTransitions.size()) {
            const YyLexerTransition& edge = lexerTransitions[static_cast<std::size_t>(candidate)];
            if (edge.check == state) {
                return edge.next;
            }
        }

        state = descriptor.def;
        if (state < 0) {
            return character < 0x10000 ? -state : 0;
        }
        if (state == 0) {
            return 0;
        }
    }
}

int RuleLexer::AcceptedAction(int state) const {
    if (state < 0 || static_cast<std::size_t>(state) >= lexerStates.size()) {
        return 0;
    }
    const int match = lexerStates[static_cast<std::size_t>(state)].match;
    if (match >= 0) {
        return match;
    }
    const std::size_t index = static_cast<std::size_t>(-match);
    if (index >= lexerMatch.size()) {
        throw std::logic_error("lexer match index is outside the exact table");
    }
    return lexerMatch[index];
}

bool RuleLexer::HasBackupMarker(int state, int action) const {
    if (state < 0 || static_cast<std::size_t>(state) >= lexerStates.size()) {
        return false;
    }
    const int match = lexerStates[static_cast<std::size_t>(state)].match;
    if (match >= 0) {
        return false;
    }
    for (std::size_t index = static_cast<std::size_t>(-match); index < lexerMatch.size() && lexerMatch[index] != 0;
        ++index) {
        if (lexerMatch[index] == -action) {
            return true;
        }
    }
    return false;
}

std::optional<RuleMatch> RuleLexer::NextRule() {
    while (true) {
        int state = startCondition_ + 1 + (endOfLine_ ? 1 : 0);
        if (state < 0 || static_cast<std::size_t>(state) >= lexerStates.size()) {
            throw std::logic_error("lexer start condition is invalid");
        }

        std::u16string text;
        std::vector<int> stateStack;
        const std::size_t startLine = engineLine_;
        oldEndOfLine_ = endOfLine_;

        while (true) {
            const int character = ReadCharacter();
            if (character == endOfInput) {
                break;
            }
            state = Transition(state, character);
            text.push_back(static_cast<char16_t>(character));
            stateStack.push_back(state);
            if (state == 0) {
                break;
            }
            const YyLexerState& descriptor = lexerStates[static_cast<std::size_t>(state)];
            if (descriptor.def == 0 && descriptor.base == 0) {
                break;
            }
        }

        while (!text.empty()) {
            const int acceptedState = stateStack.back();
            const int action = AcceptedAction(acceptedState);
            if (action > 0) {
                if (static_cast<std::size_t>(action) < lexerBackup.size() &&
                    lexerBackup[static_cast<std::size_t>(action)] != 0) {
                    while (!text.empty() && !HasBackupMarker(stateStack.back(), action)) {
                        UnreadCharacter(text.back());
                        text.pop_back();
                        stateStack.pop_back();
                    }
                }
                endOfLine_ = text.empty() ? oldEndOfLine_ : text.back() == u'\n';
                eofActionChecked_ = false;
                return RuleMatch{action, std::move(text), startLine, false};
            }

            if (text.size() > 1) {
                UnreadCharacter(text.back());
                text.pop_back();
                stateStack.pop_back();
                continue;
            }

            endOfLine_ = text.front() == u'\n';
            eofActionChecked_ = false;
            return RuleMatch{0, std::move(text), startLine, true};
        }

        if (cursor_ != input_.size() || eofActionChecked_) {
            return std::nullopt;
        }
        eofActionChecked_ = true;
        const int action = AcceptedAction(state);
        if (action > 0) {
            return RuleMatch{action, {}, startLine, false};
        }
        return std::nullopt;
    }
}

AILexer::AILexer(std::u16string_view input) : rules_(input) {
    // The original compiler sets start condition 4 before it parses. A lexer
    // reset sets 0, so this call must stay.
    rules_.SetStartCondition(4);

    const auto importWords = [this](const auto& words) {
        for (std::size_t index = 0; index < words.size(); ++index) {
            definitions_.insert_or_assign(std::u16string(words[index]), static_cast<std::uint64_t>(index));
        }
    };
    importWords(playerStateWords);
    importWords(makerStateWords);
    importWords(definitionWords);
}

void AILexer::DefineInteger(std::u16string name, std::uint64_t value) {
    definitions_.insert_or_assign(std::move(name), value);
}

void AILexer::DefinePrecompiledInteger(std::u16string name, std::int64_t value) {
    precompiledDefinitions_.insert_or_assign(std::move(name), value);
}

void AILexer::ImportPrecompiledHeader(const PrecompiledHeaderDB& database) {
    for (const auto& [name, value] : database.Names()) {
        precompiledDefinitions_.insert_or_assign(name, value);
    }
}

std::size_t AILexer::SourceLine() const noexcept { return sourceLine_; }

int AILexer::CommentDepth() const noexcept { return commentDepth_; }

std::span<const AILexerDiagnostic> AILexer::Diagnostics() const noexcept { return diagnostics_; }

AIToken AILexer::MakeToken(const RuleMatch& match, int code) const {
    return AIToken{code, match.action, match.lexeme, sourceLine_, {}, {}, false};
}

std::optional<std::uint64_t> AILexer::LookupDefinition(std::u16string_view name) const {
    const auto found = definitions_.find(name);
    if (found == definitions_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::int64_t> AILexer::LookupPrecompiledDefinition(std::u16string_view name) const {
    const auto found = precompiledDefinitions_.find(name);
    if (found == precompiledDefinitions_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void AILexer::UndefinedPrecompiledWord(std::u16string_view displayName) {
    std::u16string message(u"Undefined word[");
    message.append(displayName);
    message.append(u"] in precompiled header");
    diagnostics_.push_back(AILexerDiagnostic{sourceLine_, std::move(message)});
}

AIToken AILexer::NextToken() {
    static constexpr std::array<int, 25> keywordTokens{
        0x1001d,
        0x1001e,
        0x10022,
        0x1001c,
        0x1001f,
        0x10020,
        0x10021,
        0x10023,
        0x10024,
        0x10025,
        0x10026,
        0x1000f,
        0x10010,
        0x10011,
        0x10012,
        0x10013,
        0x10014,
        0x10015,
        0x10018,
        0x10019,
        0x1001a,
        0x10016,
        0x10017,
        0x1001b,
        0x10027,
    };

    while (const std::optional<RuleMatch> match = rules_.NextRule()) {
        const int action = match->action;
        if (match->unmatched) {
            continue; // The original lexer prints unmatched text and continues.
        }
        if (action == 1) {
            ++commentDepth_;
            continue;
        }
        if (action == 2) {
            --commentDepth_;
            if (commentDepth_ == 0) {
                rules_.SetStartCondition(4);
            }
            continue;
        }
        if (action == 3 || action == 50) {
            ++sourceLine_;
            continue;
        }
        if (action == 6) {
            rules_.SetStartCondition(2);
            commentDepth_ = 1;
            continue;
        }
        if (action >= 7 && action <= 31) {
            return MakeToken(*match, keywordTokens[static_cast<std::size_t>(action - 7)]);
        }
        if (action >= 32 && action <= 35) {
            AIToken token = MakeToken(*match, 0x10001);
            if (action == 32) {
                token.integerValue = 0;
            } else if (action == 33) {
                token.integerValue = ParseUnsigned(match->lexeme, 10);
            } else if (action == 34 && match->lexeme.size() >= 1) {
                token.integerValue = ParseUnsigned(std::u16string_view(match->lexeme).substr(1), 10);
            } else if (action == 35 && match->lexeme.size() >= 2) {
                token.integerValue = ParseUnsigned(std::u16string_view(match->lexeme).substr(2), 16);
            }
            return token;
        }
        if (action == 36) {
            AIToken token = MakeToken(*match, 0x10002);
            token.floatingValue = ParseFloat(match->lexeme);
            return token;
        }
        if (action == 37 || action == 38) {
            AIToken token = MakeToken(*match, 0x10001);
            std::u16string_view name = match->lexeme;
            std::u16string_view displayName = match->lexeme;
            if (action == 37 && name.size() >= 1U) {
                name.remove_prefix(1U);
                // Diagnostics show the original text, including the @.
            } else if (action == 38 && name.size() >= 3U) {
                name.remove_prefix(2U);
                name.remove_suffix(1U);
                // Diagnostics show a quoted name without its quotes.
                displayName = name;
            }

            if (const auto value = LookupPrecompiledDefinition(name)) {
                token.integerValue = std::bit_cast<std::uint64_t>(*value);
            } else {
                token.integerValue = std::numeric_limits<std::uint64_t>::max();
                token.unresolvedPrecompiledSymbol = true;
                UndefinedPrecompiledWord(displayName);
            }
            return token;
        }
        if (action == 39) {
            if (const auto value = LookupDefinition(match->lexeme)) {
                AIToken token = MakeToken(*match, 0x10001);
                token.integerValue = value;
                return token;
            }
            return MakeToken(*match, 0x10003);
        }
        switch (action) {
            case 40:
                return MakeToken(*match, 0x10005);
            case 41:
                return MakeToken(*match, 0x10006);
            case 42:
                return MakeToken(*match, 0x10008);
            case 43:
                return MakeToken(*match, 0x10007);
            case 44:
                return MakeToken(*match, 0x1000d);
            case 45:
                return MakeToken(*match, 0x1000e);
            case 46:
                return MakeToken(*match, 0x10009);
            case 47:
                return MakeToken(*match, 0x1000a);
            case 48:
                return MakeToken(*match, 0x1000b);
            case 49:
                return MakeToken(*match, 0x1000c);
            case 51: {
                AIToken token = MakeToken(*match, 0x10004);
                if (token.lexeme.size() >= 1001U) {
                    diagnostics_.push_back(AILexerDiagnostic{sourceLine_, u"too long string (forget right \"?)"});
                    token.lexeme = u"too long string";
                    return token;
                }
                // The value is the text without the two quotation marks.
                if (token.lexeme.size() >= 2) {
                    token.lexeme = token.lexeme.substr(1, token.lexeme.size() - 2);
                }
                return token;
            }
            case 52:
                return MakeToken(*match, match->lexeme.empty() ? 0 : static_cast<int>(match->lexeme[0]));
            default:
                break;
        }
    }
    return AIToken{0, 0, {}, sourceLine_, {}, {}, false};
}

} // namespace nasc

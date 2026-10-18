#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nasc {

class PrecompiledHeaderDB;

struct RuleMatch {
    int action = 0;
    std::u16string lexeme;
    std::size_t line = 1;
    bool unmatched = false;
};

// Runs the lexer DFA tables of NASC64_2015.exe and returns rule numbers.
// AILexer turns the rules into tokens, so the tables and the AI logic stay
// apart.
class RuleLexer {
public:
    explicit RuleLexer(std::u16string_view input);

    [[nodiscard]] std::optional<RuleMatch> NextRule();
    void SetStartCondition(int condition) noexcept;

    [[nodiscard]] int StartCondition() const noexcept;
    [[nodiscard]] std::size_t InputPosition() const noexcept;
    [[nodiscard]] std::size_t EngineLine() const noexcept;

private:
    [[nodiscard]] int ReadCharacter() noexcept;
    void UnreadCharacter(char16_t character);
    [[nodiscard]] int Transition(int state, int character) const;
    [[nodiscard]] int AcceptedAction(int state) const;
    [[nodiscard]] bool HasBackupMarker(int state, int action) const;

    std::u16string_view input_;
    std::size_t cursor_ = 0;
    std::size_t engineLine_ = 1;
    int startCondition_ = 0;
    bool endOfLine_ = true;
    bool oldEndOfLine_ = true;
    bool eofActionChecked_ = false;
};

struct AIToken {
    int code = 0;
    int lexerAction = 0;
    std::u16string lexeme;
    std::size_t line = 1;
    std::optional<std::uint64_t> integerValue;
    std::optional<float> floatingValue;
    bool unresolvedPrecompiledSymbol = false;
};

struct AILexerDiagnostic {
    std::size_t line = 1;
    std::u16string message;
};

// Turns lexer rules into AI tokens like the original lexer actions. Values,
// PCH lookups and diagnostics are returned explicitly, not kept in global
// state.
class AILexer {
public:
    explicit AILexer(std::u16string_view input);

    [[nodiscard]] AIToken NextToken();
    void DefineInteger(std::u16string name, std::uint64_t value);
    void DefinePrecompiledInteger(std::u16string name, std::int64_t value);
    void ImportPrecompiledHeader(const PrecompiledHeaderDB& database);

    [[nodiscard]] std::size_t SourceLine() const noexcept;
    [[nodiscard]] int CommentDepth() const noexcept;
    [[nodiscard]] std::span<const AILexerDiagnostic> Diagnostics() const noexcept;

private:
    [[nodiscard]] AIToken MakeToken(const RuleMatch& match, int code) const;
    [[nodiscard]] std::optional<std::uint64_t> LookupDefinition(std::u16string_view name) const;
    [[nodiscard]] std::optional<std::int64_t> LookupPrecompiledDefinition(std::u16string_view name) const;
    void UndefinedPrecompiledWord(std::u16string_view displayName);

    RuleLexer rules_;
    std::map<std::u16string, std::uint64_t, std::less<>> definitions_;
    std::map<std::u16string, std::int64_t, std::less<>> precompiledDefinitions_;
    std::vector<AILexerDiagnostic> diagnostics_;
    std::size_t sourceLine_ = 1;
    int commentDepth_ = 1;
};

} // namespace nasc

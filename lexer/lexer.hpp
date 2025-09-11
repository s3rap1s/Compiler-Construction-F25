#pragma once

#include "lexing_error.hpp"
#include "tokens.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <utility>

class Lexer {
    enum class State : char {
        Start,
        KeywordOrIdentifier,
        Identifier,
        StringLiteral,
        IntegerLiteral,
        RealLiteral,
        Punctuation
    };

    State current_state = State::Start;
    std::size_t line_no = 1;
    std::size_t char_pos = 0;
    std::string file;

    [[nodiscard]] Span getCurrentSpan(std::size_t token_start) const;

    [[nodiscard]] Token makeToken(std::size_t token_start, Token::Payload payload) const;

  public:
    explicit Lexer(std::string file) : file{std::move(file)} {}

    std::expected<std::optional<Token>, LexingError> getNextToken();
};

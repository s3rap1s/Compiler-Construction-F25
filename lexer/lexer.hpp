#pragma once

#include "lexing_error.hpp"
#include "tokens.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <utility>

namespace lexer {

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
    using ResultType = std::expected<Token, LexingError>;

    explicit Lexer(std::string file) : file{std::move(file)} {}

    // nullopt is end of stream
    std::optional<ResultType> getNextToken();
};

} // namespace lexer

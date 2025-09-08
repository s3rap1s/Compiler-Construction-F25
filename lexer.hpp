#include "tokens.hpp"

#include <cstddef>
#include <memory>
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

    State currentState = State::Start;
    std::size_t line_no = 0;
    std::size_t char_pos = 0;
    std::string file;

    [[nodiscard]] Span getCurrentSpan(std::size_t token_start) const;

    [[nodiscard]] std::shared_ptr<Token> makeBasicToken(TokenCode code, std::size_t token_start) const;

  public:
    explicit Lexer(std::string file) : file{std::move(file)} {}

    std::shared_ptr<Token> getNextToken();
};

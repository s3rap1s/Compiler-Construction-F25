#include "tokens.hpp"

#include <concepts>
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

    State current_state = State::Start;
    std::size_t line_no = 1;
    std::size_t char_pos = 0;
    std::string file;

    [[nodiscard]] Span getCurrentSpan(std::size_t token_start) const;

    template <std::derived_from<Token> T = Token>
    [[nodiscard]] std::shared_ptr<T> makeToken(TokenCode code, std::size_t token_start) const {
        auto token = std::make_shared_for_overwrite<T>();
        token->span = getCurrentSpan(token_start);
        token->code = code;
        return token;
    }

  public:
    explicit Lexer(std::string file) : file{std::move(file)} {}

    std::shared_ptr<Token> getNextToken();
};

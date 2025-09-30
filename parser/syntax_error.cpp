#include "syntax_error.hpp"

#include "lexer/tokens.hpp"
#include "utils.hpp"

#include <variant>

namespace parser {

UnexpectedTokenType::UnexpectedTokenType(const lexer::Token& token) {
    std::visit(overloaded{
        [this](const lexer::Literal&) { token_type = "literal"; },
        [this](const lexer::Identifier&) { token_type = "identifier"; },
        [this](const lexer::SyntaxPart&) { token_type = "keyword_or_operator"; },
    }, token.payload);
}

} // namespace parser

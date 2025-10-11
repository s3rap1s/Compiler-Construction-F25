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

LiteralExpected::LiteralExpected(Proxy<lexer::IntegerLiteral> /*unused*/) : expected{"integer"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::RealLiteral> /*unused*/) : expected{"real"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::BooleanLiteral> /*unused*/) : expected{"boolean"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::StringLiteral> /*unused*/) : expected{"string"} {}

}  // namespace parser

#include "semantic_error.hpp"

#include "lexer/tokens.hpp"
#include "utils.hpp"

namespace analyzer {

TokenExpected::TokenExpected(Proxy<lexer::Literal> /*unused*/) : expected{"a literal"} {}
TokenExpected::TokenExpected(Proxy<lexer::Identifier> /*unused*/) : expected{"an identifier"} {}
TokenExpected::TokenExpected(Proxy<lexer::SyntaxPart> /*unused*/) : expected{"a keyword or an operator"} {}

LiteralExpected::LiteralExpected(Proxy<lexer::IntegerLiteral> /*unused*/) : expected{"an integer"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::RealLiteral> /*unused*/) : expected{"a real"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::BooleanLiteral> /*unused*/) : expected{"a boolean"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::StringLiteral> /*unused*/) : expected{"a string"} {}

}  // namespace analyzer

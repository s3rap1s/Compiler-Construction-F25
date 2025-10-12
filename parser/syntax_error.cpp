#include "syntax_error.hpp"

#include "lexer/tokens.hpp"
#include "utils.hpp"

namespace parser {

TokenExpected::TokenExpected(Proxy<lexer::Literal> /*unused*/) : expected{"literal"} {}
TokenExpected::TokenExpected(Proxy<lexer::Identifier> /*unused*/) : expected{"identifier"} {}
TokenExpected::TokenExpected(Proxy<lexer::SyntaxPart> /*unused*/) : expected{"keyword_or_operator"} {}

LiteralExpected::LiteralExpected(Proxy<lexer::IntegerLiteral> /*unused*/) : expected{"integer"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::RealLiteral> /*unused*/) : expected{"real"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::BooleanLiteral> /*unused*/) : expected{"boolean"} {}
LiteralExpected::LiteralExpected(Proxy<lexer::StringLiteral> /*unused*/) : expected{"string"} {}

}  // namespace parser

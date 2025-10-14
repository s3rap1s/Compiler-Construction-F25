#pragma once

#include "lexer/lexing_error.hpp"
#include "lexer/tokens.hpp"
#include "utils.hpp"

#include <string>
#include <variant>
#include <vector>

namespace parser {

struct KeywordExpected {
    lexer::SyntaxPart keyword;
};

struct KeywordsExpected {
    std::vector<lexer::SyntaxPart> options;
};

struct TokenExpected {
    std::string expected;

    explicit TokenExpected(Proxy<lexer::Literal>);
    explicit TokenExpected(Proxy<lexer::Identifier>);
    explicit TokenExpected(Proxy<lexer::SyntaxPart>);
};

struct RoutineParamOrCloseParExpected {};

struct TypeExpected {};

struct LiteralExpected {
    std::string expected;

    explicit LiteralExpected(Proxy<lexer::IntegerLiteral>);
    explicit LiteralExpected(Proxy<lexer::RealLiteral>);
    explicit LiteralExpected(Proxy<lexer::BooleanLiteral>);
    explicit LiteralExpected(Proxy<lexer::StringLiteral>);
};

struct NumberLiteralExpected {};

struct PrimaryExpressionExpected {};

struct StringLiteralOrExpressionExpected {};

struct DeclarationExpected {};

struct SeparatorExpected {};

struct SyntaxError {
    using Payload = std::variant<lexer::LexingError,
                                 KeywordExpected,
                                 KeywordsExpected,
                                 TokenExpected,
                                 RoutineParamOrCloseParExpected,
                                 TypeExpected,
                                 LiteralExpected,
                                 NumberLiteralExpected,
                                 PrimaryExpressionExpected,
                                 StringLiteralOrExpressionExpected,
                                 DeclarationExpected,
                                 SeparatorExpected>;

    Span span;
    Payload payload;
};

} // namespace parser

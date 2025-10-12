#pragma once

#include "lexer/lexing_error.hpp"
#include "lexer/tokens.hpp"
#include "utils.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace parser {

struct KeywordExpected {
    lexer::SyntaxPart::Type keyword;
};

struct KeywordsExpected {
    std::vector<lexer::SyntaxPart::Type> options;
};

struct UnexpectedEndOfFile {};

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

struct SyntaxError {
    using Payload = std::variant<lexer::LexingError,
                                 KeywordExpected,
                                 KeywordsExpected,
                                 UnexpectedEndOfFile,
                                 TokenExpected,
                                 RoutineParamOrCloseParExpected,
                                 TypeExpected,
                                 LiteralExpected,
                                 NumberLiteralExpected,
                                 PrimaryExpressionExpected,
                                 StringLiteralOrExpressionExpected>;

    std::optional<Span> span;
    Payload payload;
};

} // namespace parser

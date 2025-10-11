#pragma once

#include "lexer/tokens.hpp"
#include "utils.hpp"

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

struct UnexpectedTokenType {
    std::string token_type;

    explicit UnexpectedTokenType(const lexer::Token& token);
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

using SyntaxError = std::variant<KeywordExpected,
                                 KeywordsExpected,
                                 UnexpectedEndOfFile,
                                 UnexpectedTokenType,
                                 RoutineParamOrCloseParExpected,
                                 TypeExpected,
                                 LiteralExpected,
                                 NumberLiteralExpected,
                                 PrimaryExpressionExpected>;

} // namespace parser

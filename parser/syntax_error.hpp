#pragma once

#include "lexer/tokens.hpp"

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

using SyntaxError = std::variant<KeywordExpected,
                                 KeywordsExpected,
                                 UnexpectedEndOfFile,
                                 UnexpectedTokenType,
                                 RoutineParamOrCloseParExpected,
                                 TypeExpected>;

} // namespace parser

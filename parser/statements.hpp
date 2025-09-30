#pragma once

#include "expressions.hpp"
#include "lexer/tokens.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace parser {

struct AssignmentStatement;
struct WhileStatement;
struct ForStatement;
struct IfStatement;
struct PrintStatement;
using Statement = std::variant<AssignmentStatement, WhileStatement, ForStatement, IfStatement, PrintStatement>;

using Block = std::vector<Statement>;
using StringLiteral = lexer::StringLiteral;

struct AssignmentStatement {
    ModifiablePrimary target;
    Expression expression;
};

struct IfStatement {
    Expression condition;
    Block true_branch;
    std::optional<Block> false_branch;
};

struct WhileStatement {
    Expression condition;
    Block body;
};

struct ForStatement {
    std::string identifier;
    std::variant<Expression, std::pair<Expression, Expression>> range;
    Block body;
    bool is_reverse;
};

struct PrintStatement {
    std::vector<std::variant<Expression, StringLiteral>> arguments;
};

} // namespace parser

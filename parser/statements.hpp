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
struct ReturnStatement;
using Statement = std::variant<AssignmentStatement, RoutineCall, WhileStatement, ForStatement, IfStatement, PrintStatement, ReturnStatement>;

struct VariableDeclaration;
struct TypeDeclaration;
using Block = std::vector<std::variant<VariableDeclaration, TypeDeclaration, Statement>>;

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
    std::string counter;
    std::variant<Expression, std::pair<Expression, Expression>> range;
    Block body;
    bool is_reversed;
};

struct PrintStatement {
    std::vector<std::variant<Expression, StringLiteral>> arguments;
};

struct ReturnStatement {
    std::optional<Expression> value;
};

} // namespace parser

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
struct NoopStatement {};
using Statement = std::variant<AssignmentStatement,
                               RoutineCall,
                               WhileStatement,
                               ForStatement,
                               IfStatement,
                               PrintStatement,
                               ReturnStatement,
                               NoopStatement>;

struct VariableDeclaration;
struct TypeDeclaration;
using Block = std::vector<std::variant<VariableDeclaration, TypeDeclaration, Statement>>;
// Why declarations are not considered statements?

using StringLiteral = lexer::StringLiteral;

struct AssignmentStatement : AstNode { // NOLINT(*init*)
    ModifiablePrimary target;
    Expression expression;
};

struct IfStatement : AstNode {
    Expression condition;
    Block true_branch;
    std::optional<Block> false_branch;
};

struct WhileStatement : AstNode {
    Expression condition;
    Block body;
};

struct ForStatement : AstNode {
    std::string counter;
    std::variant<Expression, std::pair<Expression, Expression>> range;
    Block body;
    bool is_reversed;
};

struct PrintStatement : AstNode {
    std::vector<std::variant<Expression, StringLiteral>> arguments;
};

struct ReturnStatement : AstNode { // NOLINT(*init*)
    std::optional<Expression> value;
};

} // namespace parser

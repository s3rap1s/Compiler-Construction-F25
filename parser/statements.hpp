#pragma once

#include "expressions.hpp"
#include "lexer/tokens.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace parser {

class AssignmentStatement;
class WhileStatement;
class ForStatement;
class IfStatement;
class PrintStatement;
using Statement = std::variant<AssignmentStatement, WhileStatement, ForStatement, IfStatement, PrintStatement>;
using Block = std::vector<Statement>;
using StringLiteral = lexer::StringLiteral;

class AssignmentStatement {
    ModifiablePrimary target;
    Expression expression;
};

class IfStatement {
    Expression condition;
    Block true_branch;
    std::optional<Block> false_branch;
};

class WhileStatement {
    Expression condition;
    Block body;
};

class ForStatement {
    std::string identifier;
    std::variant<Expression, std::pair<Expression, Expression>> range;
    Block body;
    bool is_reverse;
};

class PrintStatment {
    std::vector<std::variant<Expression, StringLiteral>> arguments;
};

} // namespace parser

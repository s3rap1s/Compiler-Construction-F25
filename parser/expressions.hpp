#pragma once

#include "lexer/tokens.hpp"
#include "parser/ast.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace parser {

using IntegerLiteral = lexer::IntegerLiteral;
using RealLiteral = lexer::RealLiteral;
using BooleanLiteral = lexer::BooleanLiteral;
struct RoutineCall;
struct ModifiablePrimary;
struct Expression;
struct UnarySign;
using Primary = std::variant<IntegerLiteral,
                             RealLiteral,
                             BooleanLiteral,
                             RoutineCall,
                             ModifiablePrimary,
                             UnarySign,
                             std::unique_ptr<Expression>>;

struct RoutineCall : AstNode {
    std::string name;
    std::vector<Expression> arguments;
};

struct ModifiablePrimary : AstNode { // NOLINT(*-special-member-*)
    std::string variable;
    std::vector<std::variant<Expression, std::string>> accessors;
};

struct UnarySign : AstNode {
    enum class Sign : char {
        Plus,
        Minus,
    };

    std::unique_ptr<Primary> operand;
    Sign sign;
};

struct Summand : AstNode {
    enum class Operation : char {
        Multiply,
        Divide,
        Modulo,
    };

    Primary first; // same as Factor
    std::vector<std::pair<Operation, Primary>> rest;
};

struct NumberExpression : AstNode { //NOLINT(*init*)
    enum class Operation : char {
        Plus,
        Minus,
    };

    Summand first;
    std::vector<std::pair<Operation, Summand>> rest;

    NumberExpression() = default; // Explicit default constructor keeps the Clang/Clangd problem away // NOLINT
};

struct Relation : AstNode {
    enum class Operation : char {
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual,
        Equal,
        NotEqual,
    };

    NumberExpression first;
    std::optional<std::pair<Operation, NumberExpression>> second;
};

struct NotExpression : AstNode {
    Primary operand;
};

using BooleanExpression = std::variant<Relation, NotExpression>;

struct Expression : AstNode {
    enum class Operation : char {
        And,
        Or,
        Xor,
    };

    BooleanExpression first;
    std::vector<std::pair<Operation, BooleanExpression>> rest;
};

} // namespace parser

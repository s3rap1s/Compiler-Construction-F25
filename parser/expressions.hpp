#pragma once

#include "lexer/tokens.hpp"

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

struct RoutineCall {
    std::string name;
    std::vector<Expression> arguments;
};

struct ModifiablePrimary { // NOLINT(*-special-member-*)
    std::string variable;
    std::vector<std::variant<Expression, std::string>> accessors;
};

struct UnarySign {
    enum class Sign : char {
        Plus,
        Minus,
    };

    std::unique_ptr<Primary> operand;
    Sign sign;
};

struct Summand {
    enum class Operation : char {
        Multiply,
        Divide,
        Modulo,
    };

    Primary first; // same as Factor
    std::vector<std::pair<Operation, Primary>> rest;
};

struct NumberExpression {
    enum class Operation : char {
        Plus,
        Minus,
    };

    Summand first;
    std::vector<std::pair<Operation, Summand>> rest;

    NumberExpression() = default; // Explicit default constructor keeps the Clang/Clangd problem away
};

struct Relation {
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

struct NotExpression {
    Primary operand;
};

using BooleanExpression = std::variant<Relation, NotExpression>;

struct Expression {
    enum class Operation : char {
        And,
        Or,
        Xor,
    };

    BooleanExpression first;
    std::vector<std::pair<Operation, BooleanExpression>> rest;
};

} // namespace parser

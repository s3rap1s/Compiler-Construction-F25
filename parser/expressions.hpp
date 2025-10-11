#pragma once

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace parser {

struct IntegerLiteral;
struct RealLiteral;
struct BooleanLiteral;
struct RoutineCall;
struct ModifiablePrimary;
using Primary = std::variant<IntegerLiteral, RealLiteral, BooleanLiteral, RoutineCall, ModifiablePrimary>;

struct Expression;
using Factor = std::variant<Primary, std::unique_ptr<Expression>>;

struct IntegerLiteral {
    long long value = 0;
};

struct RealLiteral {
    double value = 0;
};

struct BooleanLiteral {
    bool value = false;
};

struct RoutineCall {
    std::string name;
    std::vector<Expression> arguments;
};

struct ModifiablePrimary { // NOLINT(*-special-member-*)
    std::string variable;
    std::vector<std::variant<Expression, std::string>> accessors;
};

struct Summand {
    enum class Operation : char {
        Multiply,
        Divide,
        Modulo,
    };

    Factor first;
    std::vector<std::pair<Operation, Factor>> rest;
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

struct Expression {
    enum class Operation : char {
        And,
        Or,
        Xor,
    };

    Relation first;
    std::vector<std::pair<Operation, Relation>> rest;
};

} // namespace parser

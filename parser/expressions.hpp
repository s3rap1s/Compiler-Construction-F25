#pragma once

#include <memory>
#include <variant>
#include <vector>

namespace parser {

struct BinaryOperation;
struct UnaryOperation;

struct IntegerLiteral;
struct RealLiteral;
struct BooleanLiteral;
struct RoutineCall;
struct ModifiablePrimary;
using Primary = std::variant<IntegerLiteral, RealLiteral, BooleanLiteral, RoutineCall, ModifiablePrimary>;

using Expression = std::variant<BinaryOperation, UnaryOperation, Primary>;

struct BinaryOperation {
    enum struct Type : char {
        And,
        Or,
        Xor,
        LessEqual,
        Less,
        Greater,
        GreaterEqual,
        Equal,
        NotEqual,
        Multiply,
        Divide,
        Modulo,
        Plus,
        Minus,
    };
    std::unique_ptr<Expression> left, right;
};

struct UnaryOperation {
    enum struct Type : char {
        Not,
        Plus,
        Minus,
    };
    std::unique_ptr<Expression> expr;
};

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
};

struct ModifiablePrimary {
    std::string variable;
    std::vector<std::variant<Expression, std::string>> accessors;
};

} // namespace parser

#pragma once

#include <memory>
#include <variant>
#include <vector>

namespace parser {

class BinaryOperation;
class UnaryOperation;
using Expression = std::variant<BinaryOperation, UnaryOperation>;

class BinaryOperation {
    enum class Type : char {
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

class UnaryOperation {
    enum class Type : char {
        Not, 
        Plus,
        Minus,
    };
    std::unique_ptr<Expression> expr;
};

class IntegerLiteral;
class RealLiteral;
class BooleanLiteral;
class RoutineCall;
class ModifablePrimary;
using Primary = std::variant<IntegerLiteral, RealLiteral, BooleanLiteral, RoutineCall, ModifablePrimary>;

class IntegerLiteral {
    long long value = 0;
};

class RealLiteral {
    double value = 0;
};

class BooleanLiteral {
    bool value = false;
};

class RoutineCall {
    std::string name;
};

class ModifiablePrimary{
    std::string first;
    std::vector<std::variant<std::unique_ptr<Expression>, std::string>> accessors; 
};

} // namespace parser

#pragma once

#include "parser/ast.hpp"

#include <optional>
#include <variant>

namespace analyzer {

struct IntegerValue {
    long long value;
};
struct RealValue {
    double value;
};
struct BooleanValue {
    bool value;
};
using ConstexprValue = std::variant<IntegerValue, RealValue, BooleanValue>;

std::optional<ConstexprValue> computeConstexpr(const parser::Expression& expr);

} // namespace analyzer

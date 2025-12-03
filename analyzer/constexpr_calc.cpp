#include "constexpr_calc.hpp"

#include "analyzer/symbol_table.hpp"
#include "parser/ast.hpp"
#include "utils.hpp"

#include <concepts>
#include <optional>
#include <utility>
#include <variant>

#define BIND(var, monad)                                                                                               \
    auto&& var##O = monad;                                                                                             \
    if (!var##O)                                                                                                       \
        return std::nullopt;                                                                                           \
    auto&& var = *var##O;

using namespace parser;

namespace analyzer {

namespace {

IntegerValue castToInteger(const ConstexprValue& value) {
    return std::visit(overloaded{
                          [](IntegerValue i) { return i; },
                          [](RealValue r) { return IntegerValue(static_cast<decltype(IntegerValue::value)>(r.value)); },
                          [](BooleanValue b) { return IntegerValue(b.value); },
                      },
                      value);
}

RealValue castToReal(const ConstexprValue& value) {
    return std::visit(overloaded{
                          [](IntegerValue i) { return RealValue(static_cast<decltype(RealValue::value)>(i.value)); },
                          [](RealValue r) { return r; },
                          [](BooleanValue b) { return RealValue(b.value); },
                      },
                      value);
}

BooleanValue castToBoolean(const ConstexprValue& value) {
    return std::visit(overloaded{
                          [](IntegerValue i) { return BooleanValue(i.value); },
                          [](RealValue) -> BooleanValue { std::unreachable(); },
                          [](BooleanValue b) { return b; },
                      },
                      value);
}

BooleanValue expressionReducer(const ConstexprValue& a, Expression::Operator op, const ConstexprValue& b) {
    bool bool_a = castToBoolean(a).value;
    bool bool_b = castToBoolean(b).value;
    if (op == Expression::Operator::And)
        return {bool_a && bool_b};
    if (op == Expression::Operator::Or)
        return {bool_a || bool_b};
    if (op == Expression::Operator::Xor)
        return {(bool_a ^ bool_b) == 1};
    std::unreachable();
}

BooleanValue relationReducer(const ConstexprValue& a, Relation::Operator op, const ConstexprValue& b) {
    auto compare = [](auto a, auto b, Relation::Operator op) -> bool {
        if (op == Relation::Operator::Equal)
            return a == b;
        if (op == Relation::Operator::NotEqual)
            return a != b;
        if (op == Relation::Operator::Less)
            return a < b;
        if (op == Relation::Operator::LessOrEqual)
            return a <= b;
        if (op == Relation::Operator::Greater)
            return a > b;
        if (op == Relation::Operator::GreaterOrEqual)
            return a >= b;
        std::unreachable();
    };

    if (std::holds_alternative<RealValue>(a) || std::holds_alternative<RealValue>(b)) {
        RealValue real_a = castToReal(a);
        RealValue real_b = castToReal(b);
        return {compare(real_a.value, real_b.value, op)};
    }
    if (std::holds_alternative<IntegerValue>(a) || std::holds_alternative<IntegerValue>(b)) {
        IntegerValue int_a = castToInteger(a);
        IntegerValue int_b = castToInteger(b);
        return {compare(int_a.value, int_b.value, op)};
    }
    BooleanValue bool_a = castToBoolean(a);
    BooleanValue bool_b = castToBoolean(b);
    return {compare(bool_a.value, bool_b.value, op)};
}

ConstexprValue
numberExpressionReducer(const ConstexprValue& a, NumberExpression::Operator op, const ConstexprValue& b) {
    auto compute = []<typename T>(T a, T b, NumberExpression::Operator op) -> T {
        if (op == NumberExpression::Operator::Plus)
            return a + b;
        if (op == NumberExpression::Operator::Minus)
            return a - b;
        std::unreachable();
    };

    if (std::holds_alternative<RealValue>(a) || std::holds_alternative<RealValue>(b)) {
        RealValue real_a = castToReal(a);
        RealValue real_b = castToReal(b);
        return RealValue{compute(real_a.value, real_b.value, op)};
    }
    IntegerValue int_a = castToInteger(a);
    IntegerValue int_b = castToInteger(b);
    return IntegerValue{compute(int_a.value, int_b.value, op)};
}

ConstexprValue summandReducer(const ConstexprValue& a, Summand::Operator op, const ConstexprValue& b) {
    auto compute = []<typename T>(T a, T b, Summand::Operator op) -> T {
        if (op == Summand::Operator::Multiply)
            return a * b;
        if (op == Summand::Operator::Divide)
            return a / b;
        if constexpr (!std::same_as<T, decltype(RealValue::value)>)
            if (op == Summand::Operator::Modulo)
                return a % b;
        std::unreachable();
    };

    if (std::holds_alternative<RealValue>(a) || std::holds_alternative<RealValue>(b)) {
        RealValue real_a = castToReal(a);
        RealValue real_b = castToReal(b);
        return RealValue{compute(real_a.value, real_b.value, op)};
    }
    IntegerValue int_a = castToInteger(a);
    IntegerValue int_b = castToInteger(b);
    return IntegerValue{compute(int_a.value, int_b.value, op)};
}

std::optional<ConstexprValue> computeConstexpr(const Primary& primary) {
    using Ret = std::optional<ConstexprValue>;
    return std::visit(overloaded{
                          [](const IntegerLiteral& il) -> Ret { return IntegerValue{il.value}; },
                          [](const RealLiteral& rl) -> Ret { return RealValue{rl.value}; },
                          [](const BooleanLiteral& bl) -> Ret { return BooleanValue{bl.value}; },
                          [](const UnarySign& il) -> Ret {
                              BIND(expr, computeConstexpr(*il.operand));
                              int sign = il.sign == UnarySign::Sign::Plus ? 1 : -1;
                              std::visit(overloaded{[sign](IntegerValue& iv) { iv.value *= sign; },
                                                    [sign](RealValue& rv) { rv.value *= sign; },
                                                    [](BooleanValue) { std::unreachable(); }},
                                         expr);
                              return expr;
                          },
                          [](const auto&) -> Ret { return std::nullopt; },
                      },
                      primary);
}

std::optional<ConstexprValue> computeConstexpr(const Summand& summand) {
    BIND(result, computeConstexpr(summand.first));
    for (const auto& [operation, operand, _] : summand.rest) {
        BIND(computed, computeConstexpr(operand));
        result = summandReducer(result, operation, computed);
    }
    return result;
}

std::optional<ConstexprValue> computeConstexpr(const NumberExpression& expr) {
    BIND(result, computeConstexpr(expr.first));
    for (const auto& [operation, operand, _] : expr.rest) {
        BIND(computed, computeConstexpr(operand));
        result = numberExpressionReducer(result, operation, computed);
    }
    return result;
}

std::optional<ConstexprValue> computeConstexpr(const Relation& relation) {
    BIND(result, computeConstexpr(relation.first));
    if (relation.second) {
        const auto& [operation, operand] = *relation.second;
        BIND(computed, computeConstexpr(operand));
        result = relationReducer(result, operation, computed);
    }
    return result;
}

std::optional<ConstexprValue> computeConstexpr(const BooleanExpression& expr) {
    using Ret = std::optional<ConstexprValue>;
    return std::visit(overloaded{
                          [](const NotExpression& ne) -> Ret {
                              BIND(under_expr, computeConstexpr(ne.operand));
                              BooleanValue bv = castToBoolean(under_expr);
                              bv.value = !bv.value;
                              return bv;
                          },
                          [](const Relation& relation) -> Ret { return computeConstexpr(relation); },
                      },
                      expr);
}

} // namespace

std::optional<ConstexprValue> computeConstexpr(const Expression& expr) {
    BIND(result, computeConstexpr(expr.first));
    for (const auto& [operation, operand] : expr.rest) {
        BIND(computed, computeConstexpr(operand));
        result = expressionReducer(result, operation, computed);
    }
    return result;
}

parser::Expression toExpression(const ConstexprValue& value) {
    return std::visit(
        overloaded{
            [&](IntegerValue i) {
                Expression expression{
                    .first = Relation{.first =
                                          NumberExpression{
                                              Summand{Primary{IntegerLiteral{.span{}, .value = i.value}}, {}}, {}},
                                      .second{}},
                    .rest = {}};
                expression.type = SymbolTable::IntegerTypeId;
                return expression;
            },
            [&](RealValue r) {
                Expression expression{
                    .first =
                        Relation{.first =
                                     NumberExpression{Summand{Primary{RealLiteral{.span{}, .value = r.value}}, {}}, {}},
                                 .second{}},
                    .rest = {}};
                expression.type = SymbolTable::RealTypeId;
                return expression;
            },
            [&](BooleanValue b) {
                Expression expression{
                    .first = Relation{.first =
                                          NumberExpression{
                                              Summand{Primary{BooleanLiteral{.span{}, .value = b.value}}, {}}, {}},
                                      .second{}},
                    .rest = {}};
                expression.type = SymbolTable::BooleanTypeId;
                return expression;
            }},
        value);
}

} // namespace analyzer

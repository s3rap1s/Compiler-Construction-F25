#include "ast.hpp"

#include "utils.hpp"

#include <variant>

namespace parser {

Span getSpan(const Expression& expr) {
    Span first = getSpan(expr.first);
    return expr.rest.empty() ? first : first | getSpan(expr.rest.back().second);
}

Span getSpan(const BooleanExpression& expr) {
    return std::visit(overloaded{
                          [](const Relation& rel) { return getSpan(rel); },
                          [](const NotExpression& op) { return getSpan(op.operand); },
                      },
                      expr);
}

Span getSpan(const Relation& relation) {
    Span first = getSpan(relation.first);
    return !relation.second ? first : first | getSpan(relation.second->next_operand);
}

Span getSpan(const NumberExpression& expr) {
    Span first = getSpan(expr.first);
    return expr.rest.empty() ? first : first | getSpan(expr.rest.back().next_operand);
}

Span getSpan(const Summand& summand) {
    Span first = getSpan(summand.first);
    return summand.rest.empty() ? first : first | getSpan(summand.rest.back().next_operand);
}

Span getSpan(const Primary& primary) {
    return std::visit(overloaded{
                          [](const IntegerLiteral& lit) { return lit.span; },
                          [](const RealLiteral& lit) { return lit.span; },
                          [](const BooleanLiteral& lit) { return lit.span; },
                          [](const RoutineCall& call) {
                              return call.arguments.empty() ? call.routine_name.span
                                                            : call.routine_name.span | getSpan(call.arguments.back());
                          },
                          [](const ModifiablePrimary& mp) {
                              Span span = mp.variable.span;
                              if (!mp.accessors.empty())
                                  span = span | getSpan(mp.accessors.back());
                              return span;
                          },
                          [](const UnarySign& op) { return op.sign_span | getSpan(*op.operand); },
                          [](const ParenthesizedExpression& par) { return getSpan(*par.expression); },
                      },
                      primary);
}

Span getSpan(const ModifiablePrimary::Accessor& accessor) {
    return std::visit(overloaded{
                          [](const Index& i) { return i.open_bracket_span | i.close_bracket_span; },
                          [](const Identifier& id) { return id.span; },
                      },
                      accessor.key);
}

} // namespace parser

#include "analyzer.hpp"

#include "analyzer/constexpr_calc.hpp"
#include "analyzer/semantic_error.hpp"
#include "analyzer/symbol_table.hpp"
#include "parser/ast.hpp"
#include "utils.hpp"

#include <algorithm>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace analyzer {

using namespace parser;

namespace {

bool areForNumericOperation(TypeId first, TypeId second) {
    return (first == SymbolTable::IntegerTypeId || first == SymbolTable::RealTypeId) &&
           (second == SymbolTable::IntegerTypeId || second == SymbolTable::RealTypeId);
}

} // namespace

class SemanticAnalyzer {
  private:
    Program& program; // NOLINT(*ref*)
    std::string_view entry_point;
    SymbolTable table;

    const std::string& getTypeName(TypeId type) const {
        return table.getTypeInfo(type).name;
    }

    void throwTypeConversionError(TypeId from, TypeId to, Span error_span) const {
        throw SemanticError{"Type '" + getTypeName(from) + "' is not convertible to '" + getTypeName(to) + "'",
                            error_span};
    }

    void
    throwWrongOperandTypesError(TypeId first, TypeId second, std::string_view operation_type, Span error_span) const {
        std::string error = "Invalid types '" + getTypeName(first) + "' and '" + getTypeName(second) + "' for a ";
        error += operation_type;
        error += " operation";
        throw SemanticError{std::move(error), error_span};
    }

    TypeId checkExpression(Expression& expr) {
        TypeId last_type = checkBooleanExpression(expr.first);
        if (expr.rest.empty())
            return expr.type = last_type;

        for (auto& [_, operand] : expr.rest) {
            const TypeId operand_type = checkBooleanExpression(operand);
            if (last_type != table.BooleanTypeId || operand_type != table.BooleanTypeId)
                throwWrongOperandTypesError(last_type, operand_type, "boolean", getSpan(expr.first) | getSpan(operand));
            last_type = table.BooleanTypeId;
        }
        return expr.type = last_type;
    }

    TypeId checkBooleanExpression(BooleanExpression& bool_expr) {
        return std::visit(overloaded{[this](Relation& relation) { return checkRelation(relation); },
                                     [this](NotExpression& not_expr) { return checkNotExpression(not_expr); }},
                          bool_expr);
    }

    TypeId checkRelation(Relation& relation) {
        TypeId first = checkNumberExpression(relation.first);
        if (!relation.second)
            return relation.type = first;
        TypeId second = checkNumberExpression(relation.second->next_operand);
        if (!areForNumericOperation(first, second)) {
            throwWrongOperandTypesError(
                first, second, "numeric", getSpan(relation.first) | getSpan(relation.second->next_operand));
        }
        return relation.type = table.BooleanTypeId;
    }

    TypeId checkNotExpression(NotExpression& not_expr) {
        TypeId type = checkPrimary(not_expr.operand);
        if (type != table.IntegerTypeId && type != table.BooleanTypeId) {
            throw SemanticError{"Invalid operand type '" + std::string{getTypeName(type)} + "' for 'not' operation",
                                not_expr.not_span};
        }
        return table.BooleanTypeId;
    }

    TypeId checkNumberExpression(NumberExpression& num_expr) {
        TypeId last_type = checkSummand(num_expr.first);
        for (auto& [_, operand, operation_type] : num_expr.rest) {
            const TypeId operand_type = checkSummand(operand);
            if (!areForNumericOperation(last_type, operand_type)) {
                throwWrongOperandTypesError(
                    last_type, operand_type, "numeric", getSpan(num_expr.first) | getSpan(operand));
            }
            last_type = operation_type = (last_type == table.RealTypeId || operand_type == table.RealTypeId)
                                             ? table.RealTypeId
                                             : table.IntegerTypeId;
        }
        return num_expr.type = last_type;
    }

    TypeId checkSummand(Summand& summand) {
        TypeId last_type = checkPrimary(summand.first);
        for (auto& [op, operand, operation_type] : summand.rest) {
            const TypeId operand_type = checkPrimary(operand);
            if (!areForNumericOperation(last_type, operand_type) ||
                (op == Summand::Operator::Modulo && operand_type == table.RealTypeId)) {
                throwWrongOperandTypesError(
                    last_type, operand_type, "numeric", getSpan(summand.first) | getSpan(operand));
            }
            last_type = operation_type = (last_type == table.RealTypeId || operand_type == table.RealTypeId)
                                             ? table.RealTypeId
                                             : table.IntegerTypeId;
        }
        return summand.type = last_type;
    }

    TypeId checkPrimary(Primary& primary) {
        return std::visit(overloaded{
                              [](const IntegerLiteral&) { return SymbolTable::IntegerTypeId; },
                              [](const RealLiteral&) { return SymbolTable::RealTypeId; },
                              [](const BooleanLiteral&) { return SymbolTable::BooleanTypeId; },
                              [this](RoutineCall& call) {
                                  std::optional<TypeId> type = checkRoutineCall(call);
                                  if (!type) {
                                      throw SemanticError{"Trying to use void procedure as an expression",
                                                          call.routine_name.span};
                                  }
                                  return *type;
                              },
                              [this](ModifiablePrimary& mp) { return checkModifiablePrimary(mp); },
                              [this](UnarySign& op) { return op.type = checkPrimary(*op.operand); },
                              [this](ParenthesizedExpression& par) { return checkExpression(*par.expression); },
                          },
                          primary);
    }

    TypeId checkModifiablePrimary(ModifiablePrimary& mp) {
        table.ensureVarExists(mp);
        table.markVarUsed(mp.variable.text);
        mp.variable_type = table.getVariableType(mp.variable);
        if (!mp.accessors.empty()) {
            TypeId current_type = mp.variable_type;
            for (ModifiablePrimary::Accessor& accessor : mp.accessors) {
                current_type = checkAccessor(current_type, accessor);
            }
            return current_type;
        }
        return mp.variable_type;
    }

    TypeId checkAccessor(TypeId type_of_last, ModifiablePrimary::Accessor& accessor) {
        return std::visit(
            overloaded{[&](Identifier& field_name) { return checkField(type_of_last, accessor.type, field_name); },
                       [&](Index& index) { return checkIndex(type_of_last, accessor.type, index); }},
            accessor.key);
    }

    TypeId checkField(TypeId type_id_of_last, TypeId& accessor_type, Identifier& field_name) {
        const TypeInfo& type_info_of_last = table.getTypeInfo(type_id_of_last);

        if (std::holds_alternative<ArrayTypeInfo>(type_info_of_last.definition)) {
            if (field_name.text == "size")
                return accessor_type = table.IntegerTypeId;
            throw SemanticError{"Type " + type_info_of_last.name + " has no field named '" + field_name.text + "'",
                                field_name.span};
        }

        if (!std::holds_alternative<RecordTypeInfo>(type_info_of_last.definition))
            throw SemanticError{"Cannot access field '" + field_name.text + "' on non-record type", field_name.span};

        const auto& record_info = std::get<RecordTypeInfo>(type_info_of_last.definition);
        auto it =
            std::ranges::find(record_info.fields, field_name.text, &decltype(record_info.fields)::value_type::first);
        if (it != record_info.fields.end())
            return accessor_type = it->second;
        throw SemanticError{"Type " + type_info_of_last.name + " has no field named '" + field_name.text + "'",
                            field_name.span};
    }

    TypeId checkIndex(TypeId type_id_of_last, TypeId& accessor_type, Index& index) {
        const TypeInfo& type_info_of_last = table.getTypeInfo(type_id_of_last);

        if (!std::holds_alternative<ArrayTypeInfo>(type_info_of_last.definition))
            throw SemanticError{"Cannot index non-array type", index.bracket_span};

        checkExpression(index.value);
        if (index.value.type != table.IntegerTypeId)
            throw SemanticError{"Array index must be of integer type", index.bracket_span};

        return accessor_type = std::get<ArrayTypeInfo>(type_info_of_last.definition).element_type;
    }

    std::optional<TypeId> checkRoutineCall(RoutineCall& call) {
        auto routine_it = table.getRoutines().find(call.routine_name.text);
        if (routine_it == table.getRoutines().end())
            throw SemanticError{"Undeclared routine: " + call.routine_name.text, call.routine_name.span};

        RoutineInfo& routine_info = routine_it->second;
        routine_info.used = true;

        if (call.arguments.size() != routine_info.parameters.size()) {
            throw SemanticError{std::format("Routine '{}' expects {} arguments, but {} are provided",
                                            call.routine_name.text,
                                            routine_info.parameters.size(),
                                            call.arguments.size()),
                                call.routine_name.span};
        }

        for (auto [param_type, arg] : std::views::zip(routine_info.parameters, call.arguments)) {
            checkExpression(arg);
            if (!table.isConvertibleTo(arg.type, param_type))
                throwTypeConversionError(arg.type, param_type, getSpan(arg));
        }

        return call.type = routine_info.return_type;
    }

    void checkVariableDeclaration(VariableDeclaration& var) {
        if (var.value)
            checkExpression(*var.value);
        if (var.type)
            var.resolved_type = checkType(*var.type);
        else
            var.resolved_type = var.value->type;
    }

    TypeId checkType(Type& type) {
        std::visit(overloaded{[this](const Identifier& type_name) {
                                  table.ensureTypeExists(type_name);
                                  table.markTypeUsed(type_name.text);
                              },
                              [this](ArrayType& array) { checkArrayType(array); },
                              [this](RecordType& record) {
                                  for (VariableDeclaration& field : record.fields) {
                                      checkVariableDeclaration(field);
                                  }
                              },
                              [](const auto&) {}},
                   type);
        return table.resolveType(type);
    }

    void checkArrayType(ArrayType& array) {
        array.resolved_element_type = checkType(*array.element_type);
        if (array.size) {
            checkExpression(*array.size);
            if (array.size->type != SymbolTable::IntegerTypeId)
                throw SemanticError{"Array size must be an integer", getSpan(*array.size)};
            std::optional<ConstexprValue> computed_size = computeConstexpr(*array.size);
            if (!computed_size)
                throw SemanticError{"Array size must be a constant expression", getSpan(*array.size)};
            auto* integer = std::get_if<IntegerValue>(&*computed_size);
            if (!integer)
                throw SemanticError{"Array size must be an integer", getSpan(*array.size)};
            if (integer->value <= 0)
                throw SemanticError{std::format("Array size should be positive, given {}", integer->value),
                                    getSpan(*array.size)};
            array.computed_size = integer->value;
        }
    }

    void checkBlock(Block& block) {
        table.pushScope(block);
        for (Statement& element : block)
            checkStatement(element);
        table.popScope(block);
    }

    void checkStatement(Statement& stmt) {
        std::visit(overloaded{
                       [&](VariableDeclaration& var) {
                           checkVariableDeclaration(var);
                           table.addLocalVariable(var.name, var.resolved_type);
                       },
                       [&](TypeDeclaration& type) {
                           TypeId type_id = checkType(type.type);
                           table.addLocalTypeDeclaration(type.name, type_id);
                       },
                       [this](AssignmentStatement& statement) { checkAssignment(statement); },
                       [this](WhileStatement& statement) {
                           checkExpression(statement.condition);
                           checkBlock(statement.body);
                       },
                       [this](ForStatement& statement) { checkForStatement(statement); },
                       [this](IfStatement& statement) {
                           checkExpression(statement.condition);
                           checkBlock(statement.true_branch);
                           if (statement.false_branch) {
                               checkBlock(*statement.false_branch);
                           }
                       },
                       [this](PrintStatement& statement) {
                           for (auto& arg : statement.arguments) {
                               if (std::holds_alternative<Expression>(arg)) {
                                   checkExpression(std::get<Expression>(arg));
                               }
                           }
                       },
                       [this](ReturnStatement& statement) {
                           if (statement.value) {
                               checkExpression(*statement.value);
                           }
                       },
                       [this](RoutineCall& statement) { checkRoutineCall(statement); },
                   },
                   stmt);
    }

    void checkAssignment(AssignmentStatement& assignment) {
        TypeId target_type = checkModifiablePrimary(assignment.target);
        TypeId expr_type = checkExpression(assignment.expression);

        const Identifier& target_var = assignment.target.variable;
        if (assignment.target.accessors.empty() && table.getForLoopVariables().contains(target_var.text))
            throw SemanticError{"Cannot assign to for loop variable: " + target_var.text, target_var.span};

        if (!table.isConvertibleTo(expr_type, target_type))
            throwTypeConversionError(expr_type, target_type, getSpan(assignment.expression));
    }

    void checkForStatement(ForStatement& for_stmt) {
        table.pushScope(for_stmt.body);

        const bool is_foreach = std::holds_alternative<Expression>(for_stmt.range);
        TypeId variable_type = -1;

        if (is_foreach) {
            auto& iterable = std::get<Expression>(for_stmt.range);
            TypeId array_type_id = checkExpression(iterable);
            const TypeInfo& array_type_info = table.getTypeInfo(array_type_id);

            if (!std::holds_alternative<ArrayTypeInfo>(array_type_info.definition))
                throw SemanticError{"Iterable must of an array type", getSpan(iterable)};

            variable_type = std::get<ArrayTypeInfo>(array_type_info.definition).element_type;
        } else {
            auto& range = std::get<std::pair<Expression, Expression>>(for_stmt.range);
            TypeId first_type = checkExpression(range.first);
            TypeId second_type = checkExpression(range.second);

            if (first_type != table.IntegerTypeId)
                throw SemanticError{"For loop range must be of integer type", getSpan(range.first)};
            if (second_type != table.IntegerTypeId)
                throw SemanticError{"For loop range must be of integer type", getSpan(range.second)};

            variable_type = table.IntegerTypeId;
        }

        table.addLocalVariable(for_stmt.variable_name, variable_type);
        table.markVarUsed(for_stmt.variable_name.text);
        if (table.getForLoopVariables().contains(for_stmt.variable_name.text))
            throw SemanticError{"Do not create for loop variables with the same name", for_stmt.variable_name.span};
        table.getForLoopVariables().insert(for_stmt.variable_name.text);

        checkBlock(for_stmt.body);

        table.getForLoopVariables().erase(for_stmt.variable_name.text);
        table.popScope(for_stmt.body);
    }

    // @return Whether the last statement is return
    void checkRoutineDeclaration(RoutineDeclaration& routine) {
        for (ParameterDeclaration& param : routine.parameters)
            param.resolved_type = checkType(param.type);
        if (routine.return_type)
            routine.resolved_return_type = checkType(*routine.return_type);
        if (!routine.body && !routine.return_type)
            routine.resolved_return_type = std::nullopt;
    }

    bool checkRoutineDefinition(RoutineDeclaration& routine) {
        if (!routine.body)
            return false;

        if (auto* expr = std::get_if<Expression>(&*routine.body)) {
            Block block;
            block.emplace_back(ReturnStatement{.return_span = routine.name.span, .value = std::move(*expr)});
            *routine.body = std::move(block);
        }

        auto& body = std::get<Block>(*routine.body);
        table.pushScope(body);
        for (const ParameterDeclaration& param : routine.parameters)
            table.addLocalVariable(param.name, param.resolved_type);
        checkBlock(body);
        if (routine.return_type)
            checkReturnType(routine, body);
        else {
            DeducingReturnTypeState deducing_state;
            deduceReturnType(routine, body, deducing_state);
            if (deducing_state.return_type != static_cast<TypeId>(-1))
                routine.resolved_return_type = deducing_state.return_type;
            else
                routine.resolved_return_type = std::nullopt;
        }
        table.popScope(body);

        bool last_return = false;
        if (!body.empty())
            last_return = std::holds_alternative<ReturnStatement>(body.back());
        if (routine.return_type && !last_return)
            throw SemanticError{"Non-void function must have return as the last statement", routine.name.span};
        return last_return;
    }

    void checkReturnType(RoutineDeclaration& routine, Block& body) {
        for (Statement& statement : body) {
            std::visit(overloaded{
                           [&](ReturnStatement& return_stmt) {
                               if (!return_stmt.value) {
                                   throw SemanticError{"Function '" + routine.name.text + "' must return a value",
                                                       return_stmt.return_span};
                               }
                               if (!table.isConvertibleTo(return_stmt.value->type, *routine.resolved_return_type)) {
                                   throwTypeConversionError(
                                       return_stmt.value->type, *routine.resolved_return_type, return_stmt.return_span);
                               }
                           },
                           [&](WhileStatement& while_loop) { checkReturnType(routine, while_loop.body); },
                           [&](ForStatement& for_loop) { checkReturnType(routine, for_loop.body); },
                           [&](IfStatement& if_stmt) {
                               checkReturnType(routine, if_stmt.true_branch);
                               if (if_stmt.false_branch)
                                   checkReturnType(routine, *if_stmt.false_branch);
                           },
                           [](const auto&) {},
                       },
                       statement);
        }
    }

    struct DeducingReturnTypeState {
        TypeId return_type = -1;
        bool seen_empty_return = false;
    };

    void
    deduceReturnType(RoutineDeclaration& routine, Block& body, DeducingReturnTypeState& state) { // NOLINT(*complexity)
        for (Statement& statement : body) {
            std::visit(
                overloaded{
                    [&](ReturnStatement& return_stmt) {
                        if (return_stmt.value) {
                            if (state.seen_empty_return) {
                                throw SemanticError{
                                    "Function returns a value here but an empty return was seen earlier",
                                    return_stmt.return_span};
                            }
                            if (state.return_type == static_cast<TypeId>(-1)) { // deduce type first time here
                                if (return_stmt.value->type == static_cast<TypeId>(-1))
                                    throw SemanticError{"Cannot deduce return type here", getSpan(*return_stmt.value)};
                                state.return_type = return_stmt.value->type;
                            } else if (state.return_type != return_stmt.value->type) {
                                throw SemanticError{"Function returns '" + getTypeName(return_stmt.value->type) +
                                                        "' here but earlier returns '" +
                                                        getTypeName(state.return_type) + "'",
                                                    return_stmt.return_span};
                            }
                        } else {
                            state.seen_empty_return = true;
                            if (state.return_type != static_cast<TypeId>(-1)) {
                                throw SemanticError{"Function returns nothing here but return type was deduced as '" +
                                                        getTypeName(state.return_type) + "' earlier",
                                                    return_stmt.return_span};
                            }
                        }
                    },
                    [&](WhileStatement& while_loop) { deduceReturnType(routine, while_loop.body, state); },
                    [&](ForStatement& for_loop) { deduceReturnType(routine, for_loop.body, state); },
                    [&](IfStatement& if_stmt) {
                        deduceReturnType(routine, if_stmt.true_branch, state);
                        if (if_stmt.false_branch)
                            deduceReturnType(routine, *if_stmt.false_branch, state);
                    },
                    [](const auto&) {},
                },
                statement);
        }
    }

    void checkForwardDeclarations() {
        for (const auto& [name, routine_info] : table.getRoutines()) {
            if (!routine_info.defined) {
                throw SemanticError{"Forward declared routine '" + name + "' is never defined",
                                    routine_info.span_of_declaration};
            }
        }
    }

    bool optimizeBlock(Block& block) {
        auto block_usage_it = table.getScopes().find(&block);
        Block optimized;
        for (bool found_return = false; Statement& statement : block) {
            if (found_return)
                break;
            // optimize out?
            if (!optimizeStatement(statement, block_usage_it->second))
                optimized.emplace_back(std::move(statement));
            if (std::holds_alternative<ReturnStatement>(statement))
                found_return = true;
        }
        block = std::move(optimized);
        return block.empty();
    }

    bool optimizeStatement(Statement& statement, const Scope& scope) {
        return std::visit(
            overloaded{
                [&](VariableDeclaration& var) { return !scope.variables.at(var.name.text).used && !var.value; },
                [&](TypeDeclaration& type) { return !table.getTypeInfo(scope.types.at(type.name.text)).used; },
                [this](WhileStatement& while_loop) { return optimizeBlock(while_loop.body); },
                [this](ForStatement& for_loop) { return optimizeBlock(for_loop.body); },
                [this](IfStatement& if_stmt) {
                    if (!if_stmt.false_branch)
                        return optimizeBlock(if_stmt.true_branch);
                    return optimizeBlock(if_stmt.true_branch) && optimizeBlock(*if_stmt.false_branch);
                },
                [](auto& /*statement*/) { return false; },
            },
            statement);
    }

    void optimizeRoutine(RoutineDeclaration& routine) {
        if (!routine.body)
            return;

        if (std::holds_alternative<Block>(*routine.body)) {
            optimizeBlock(std::get<Block>(*routine.body));
        }
    }

    void optimizeProgram() {
        if (auto it = table.getRoutines().find(std::string{entry_point}); it != table.getRoutines().end())
            it->second.used = true;

        auto it = program.declarations.begin();
        while (it != program.declarations.end()) {
            if (std::holds_alternative<VariableDeclaration>(*it)) {
                const auto& var_decl = std::get<VariableDeclaration>(*it);
                if (!table.getGlobalScope().variables.at(var_decl.name.text).used) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<TypeDeclaration>(*it)) {
                const auto& type_decl = std::get<TypeDeclaration>(*it);
                if (!table.getTypeInfo(table.getGlobalScope().types.at(type_decl.name.text)).used) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<RoutineDeclaration>(*it)) {
                auto& routine_decl = std::get<RoutineDeclaration>(*it);
                if (!table.getRoutines().at(routine_decl.name.text).used) {
                    it = program.declarations.erase(it);
                    continue;
                }
                optimizeRoutine(routine_decl);
            }
            ++it;
        }
    }

    void checkProgram() {
        for (auto& decl : program.declarations) {
            std::visit(overloaded{
                           [this](VariableDeclaration& var) {
                               checkVariableDeclaration(var);
                               table.addLocalVariable(var.name, var.resolved_type);
                           },
                           [this](TypeDeclaration& type) {
                               TypeId type_id = checkType(type.type);
                               table.addLocalTypeDeclaration(type.name, type_id);
                           },
                           [this](RoutineDeclaration& routine) {
                               auto it = table.getRoutines().find(routine.name.text);
                               if (it != table.getRoutines().end()) {
                                   if (it->second.defined && routine.body)
                                       throw SemanticError{"Duplicate routine definition: " + routine.name.text,
                                                           routine.name.span};
                                   if (routine.body)
                                       it->second.defined = true;
                                   it->second.last_return = checkRoutineDefinition(routine);
                               } else {
                                   checkRoutineDeclaration(routine);
                                   bool is_defined = routine.body.has_value();
                                   RoutineInfo info{.parameters = {},
                                                    .return_type = routine.resolved_return_type,
                                                    .span_of_declaration = routine.name.span,
                                                    .last_return = false,
                                                    .defined = is_defined};
                                   for (const ParameterDeclaration& param : routine.parameters)
                                       info.parameters.push_back(param.resolved_type);
                                   auto [it, _] = table.getRoutines().emplace(routine.name.text, std::move(info));
                                   bool last_return = checkRoutineDefinition(routine);
                                   it->second.last_return = last_return;
                               }
                           },
                       },
                       decl);
        }
        checkForwardDeclarations();
    }

  public:
    explicit SemanticAnalyzer(Program& program, std::string_view entry_point)
        : program{program}, entry_point{entry_point} {}

    std::expected<SymbolTable, SemanticError> analyze() {
        try {
            checkProgram();
        } catch (const SemanticError& error) {
            return std::unexpected<SemanticError>(error);
        }
        optimizeProgram(); // should never throw SemanticError
        return std::move(table);
    }
};

std::expected<SymbolTable, SemanticError> analyze(Program& ast, std::string_view entry_point) {
    SemanticAnalyzer analyzer{ast, entry_point};
    return analyzer.analyze();
}

} // namespace analyzer

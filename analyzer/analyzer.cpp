#include "analyzer.hpp"

#include "analyzer/semantic_error.hpp"
#include "analyzer/symbol_table.hpp"
#include "parser/ast.hpp"
#include "parser/syntax_error.hpp"
#include "utils.hpp"

#include <algorithm>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
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

    TypeId checkExpression(Expression& expr) {
        TypeId last_type = checkBooleanExpression(expr.first);
        if (expr.rest.empty())
            return expr.type = last_type;

        for (auto& [_, operand] : expr.rest) {
            const TypeId operand_type = checkBooleanExpression(operand);
            if (last_type != table.BooleanTypeId || operand_type != table.BooleanTypeId) {
                const TypeInfo& info1 = table.getTypeInfo(last_type);
                const TypeInfo& info2 = table.getTypeInfo(operand_type);
                throw SemanticError{"Invalid types '" + info1.name + "' and '" + info2.name + "' for boolean operation",
                                    getSpan(expr)};
            }
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
            const TypeInfo& info1 = table.getTypeInfo(first);
            const TypeInfo& info2 = table.getTypeInfo(second);
            throw SemanticError{"Invalid types '" + info1.name + "' and '" + info2.name + "' for numeric operation",
                                getSpan(relation)};
        }
        return relation.type = table.BooleanTypeId;
    }

    TypeId checkNotExpression(NotExpression& not_expr) {
        TypeId type = checkPrimary(not_expr.operand);
        if (type != table.IntegerTypeId && type != table.BooleanTypeId) {
            throw SemanticError{"Invalid operand type '" + table.getTypeInfo(type).name + "' for numeric operation",
                                not_expr.not_span};
        }
        return table.BooleanTypeId;
    }

    TypeId checkNumberExpression(NumberExpression& num_expr) {
        TypeId last_type = checkSummand(num_expr.first);
        for (auto& [_, operand, operation_type] : num_expr.rest) {
            const TypeId operand_type = checkSummand(operand);
            if (!areForNumericOperation(last_type, operand_type)) {
                const TypeInfo& info1 = table.getTypeInfo(last_type);
                const TypeInfo& info2 = table.getTypeInfo(operand_type);
                throw SemanticError{"Invalid types '" + info1.name + "' and '" + info2.name + "' for numeric operation",
                                    getSpan(num_expr)};
            }
            if (last_type == table.RealTypeId || operand_type == table.RealTypeId)
                operation_type = table.RealTypeId;
            else
                operation_type = table.IntegerTypeId;
            last_type = operand_type;
        }
        return num_expr.type = last_type;
    }

    TypeId checkSummand(Summand& summand) {
        TypeId last_type = checkPrimary(summand.first);
        for (auto& [op, operand, operation_type] : summand.rest) {
            const TypeId operand_type = checkPrimary(operand);
            if (!areForNumericOperation(last_type, operand_type) ||
                (op == Summand::Operator::Modulo && operand_type == table.RealTypeId)) {
                const TypeInfo& info1 = table.getTypeInfo(last_type);
                const TypeInfo& info2 = table.getTypeInfo(operand_type);
                throw SemanticError{"Invalid types '" + info1.name + "' and '" + info2.name + "' for operation",
                                    getSpan(summand)};
            }
            if (last_type == table.RealTypeId || operand_type == table.RealTypeId)
                operation_type = table.RealTypeId;
            else
                operation_type = table.IntegerTypeId;
            last_type = operand_type;
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
            if (!table.isConvertibleTo(arg.type, param_type)) {
                throw SemanticError{"Type '" + table.getTypeInfo(arg.type).name + "' is not convertible to '" +
                                        table.getTypeInfo(param_type).name + "'",
                                    getSpan(arg)};
            }
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
                              [this](ArrayType& array) {
                                  array.resolved_element_type = checkType(*array.element_type);
                                  if (array.size)
                                      checkExpression(*array.size);
                                  // TODO: compute size
                                  array.computed_size = 100; // NOLINT
                              },
                              [this](RecordType& record) {
                                  for (VariableDeclaration& field : record.fields) {
                                      checkVariableDeclaration(field);
                                  }
                              },
                              [](const auto&) {}},
                   type);
        return table.resolveType(type);
    }

    void checkBlock(Block& block) {
        table.pushScope(block);
        for (auto& element : block) {
            std::visit(overloaded{[&](VariableDeclaration& var) {
                                      checkVariableDeclaration(var);
                                      table.addLocalVariable(var.name, var.resolved_type);
                                  },
                                  [&](TypeDeclaration& type) {
                                      TypeId type_id = checkType(type.type);
                                      table.addLocalTypeDeclaration(type.name, type_id);
                                  },
                                  [&](Statement& stmt) { checkStatement(stmt); }},
                       element);
        }
        table.popScope(block);
    }

    void optimizeBlock(Block& block) { // NOLINT(*complexity*)
        auto block_usage_it = table.getScopes().find(&block);
        if (block_usage_it == table.getScopes().end())
            return;

        const std::unordered_map<std::string, VarInfo>& variable_usage = block_usage_it->second.variables;
        const std::unordered_map<std::string, TypeId>& type_usage = block_usage_it->second.types;

        Block optimized;
        for (bool found_return = false; auto& element : block) {
            if (found_return)
                break;
            std::visit(overloaded{
                           [&](VariableDeclaration& var) {
                               if ((variable_usage.contains(var.name.text) && variable_usage.at(var.name.text).used) ||
                                   var.value) {
                                   optimized.emplace_back(std::move(var));
                               }
                           },
                           [&](TypeDeclaration& type) {
                               if (type_usage.contains(type.name.text) &&
                                   table.getTypeInfo(type_usage.at(type.name.text)).used) {
                                   optimized.emplace_back(std::move(type));
                               }
                           },
                           [&](Statement& statement) {
                               optimizeStatement(statement);
                               if (std::holds_alternative<ReturnStatement>(statement))
                                   found_return = true;
                               optimized.emplace_back(std::move(statement));
                           },
                       },
                       element);
        }
        block = std::move(optimized);
    }

    void checkStatement(Statement& stmt) {
        std::visit(
            [this](auto& statement) {
                using T = std::decay_t<decltype(statement)>;

                if constexpr (std::is_same_v<T, AssignmentStatement>) {
                    checkAssignment(statement);
                } else if constexpr (std::is_same_v<T, WhileStatement>) {
                    checkExpression(statement.condition);
                    checkBlock(statement.body);
                } else if constexpr (std::is_same_v<T, ForStatement>) {
                    checkForStatement(statement);
                } else if constexpr (std::is_same_v<T, IfStatement>) {
                    checkExpression(statement.condition);
                    checkBlock(statement.true_branch);
                    if (statement.false_branch) {
                        checkBlock(*statement.false_branch);
                    }
                } else if constexpr (std::is_same_v<T, PrintStatement>) {
                    for (auto& arg : statement.arguments) {
                        if (std::holds_alternative<Expression>(arg)) {
                            checkExpression(std::get<Expression>(arg));
                        }
                    }
                } else if constexpr (std::is_same_v<T, ReturnStatement>) {
                    if (statement.value) {
                        checkExpression(*statement.value);
                    }
                } else if constexpr (std::is_same_v<T, RoutineCall>) {
                    checkRoutineCall(statement);
                }
            },
            stmt);
    }

    void optimizeStatement(Statement& stmt) {
        std::visit(
            [&](auto& statement) {
                using T = std::decay_t<decltype(statement)>;

                if constexpr (std::is_same_v<T, WhileStatement> || std::is_same_v<T, ForStatement>) {
                    optimizeBlock(statement.body);
                } else if constexpr (std::is_same_v<T, IfStatement>) {
                    optimizeBlock(statement.true_branch);
                    if (statement.false_branch)
                        optimizeBlock(*statement.false_branch);
                }
            },
            stmt);
    }

    void checkAssignment(AssignmentStatement& assignment) {
        TypeId target_type = checkModifiablePrimary(assignment.target);
        TypeId expr_type = checkExpression(assignment.expression);

        const Identifier& target_var = assignment.target.variable;
        if (assignment.target.accessors.empty() && table.getForLoopVariables().contains(target_var.text))
            throw SemanticError{"Cannot assign to for loop variable: " + target_var.text, target_var.span};

        if (!table.isConvertibleTo(expr_type, target_type)) {
            const TypeInfo& target_info = table.getTypeInfo(target_type);
            const TypeInfo& expr_info = table.getTypeInfo(expr_type);
            throw SemanticError{"Type '" + expr_info.name + "' is not convertible to '" + target_info.name + "'",
                                getSpan(assignment.expression)};
        }
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

    bool checkRoutineDeclaration(RoutineDeclaration& routine) {
        for (ParameterDeclaration& param : routine.parameters)
            param.resolved_type = checkType(param.type);
        if (routine.return_type)
            routine.return_type->resolved = checkType(routine.return_type->type);

        if (!routine.body)
            return false;

        if (auto* expr = std::get_if<Expression>(&*routine.body)) {
            if (!routine.return_type)
                throw SemanticError{"Arrow function must specify return type", getSpan(*expr)};

            TypeId expr_type = checkExpression(*expr);
            if (!table.isConvertibleTo(expr_type, routine.return_type->resolved)) {
                const TypeInfo& return_type_info = table.getTypeInfo(routine.return_type->resolved);
                const TypeInfo& expr_info = table.getTypeInfo(expr_type);
                throw SemanticError{"Type '" + expr_info.name + "' is not convertible to '" + return_type_info.name +
                                        "'",
                                    getSpan(*expr)};
            }

            Block block;
            block.emplace_back(ReturnStatement{.value = std::move(*expr)});
            *routine.body = std::move(block);
        }

        auto& body = std::get<Block>(*routine.body);
        table.pushScope(body);
        for (const ParameterDeclaration& param : routine.parameters)
            table.addLocalVariable(param.name, param.resolved_type);
        checkBlock(body);

        bool last_return = false;
        if (!body.empty()) {
            if (auto* last_statement = std::get_if<Statement>(&body.back()))
                last_return = std::holds_alternative<ReturnStatement>(*last_statement);
        }
        if (routine.return_type && !last_return)
            throw SemanticError{"Non-void function must have return as the last statement", routine.name.span};

        table.popScope(body);
        return last_return;
    }

    void optimizeRoutine(RoutineDeclaration& routine) {
        if (!routine.body)
            return;

        if (std::holds_alternative<Block>(*routine.body)) {
            optimizeBlock(std::get<Block>(*routine.body));
        }
    }

    void checkForwardDeclarations() {
        for (const auto& [name, routine_info] : table.getRoutines()) {
            if (!routine_info.defined) {
                throw SemanticError{"Forward declared routine \"" + name + "\" is never defined",
                                    routine_info.span_of_declaration};
            }
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
                                   checkRoutineDeclaration(routine);
                               } else {
                                   bool last_return = checkRoutineDeclaration(routine);
                                   bool is_defined = routine.body.has_value();
                                   const auto& return_type = routine.return_type;
                                   RoutineInfo info{.parameters = {},
                                                    .return_type = return_type ? std::optional{return_type->resolved}
                                                                               : std::nullopt,
                                                    .span_of_declaration = routine.name.span,
                                                    .last_return = last_return,
                                                    .defined = is_defined};
                                   for (const ParameterDeclaration& param : routine.parameters)
                                       info.parameters.push_back(param.resolved_type);
                                   table.getRoutines().emplace(routine.name.text, std::move(info));
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

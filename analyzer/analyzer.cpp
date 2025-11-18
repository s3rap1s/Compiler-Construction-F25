#include "analyzer.hpp"

#include "analyzer/semantic_error.hpp"
#include "analyzer/symbol_table.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"
#include "utils.hpp"

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

class SemanticAnalyzer {
  private:
    Program& program; // NOLINT(*ref*)
    std::string_view entry_point;
    SymbolTable table;

    void checkExpression(const Expression& expr) {
        checkBooleanExpression(expr.first);

        for (const auto& [op, bool_expr] : expr.rest) {
            checkBooleanExpression(bool_expr);
        }
    }

    void checkBooleanExpression(const BooleanExpression& bool_expr) {
        std::visit(overloaded{
                       [this](const Relation& relation) {
                           checkNumberExpression(relation.first);
                           if (relation.second)
                               checkNumberExpression(relation.second->second);
                       },
                       [this](const NotExpression& notExpr) { checkPrimary(notExpr.operand); },
                   },
                   bool_expr);
    }

    void checkNumberExpression(const NumberExpression& num_expr) {
        checkSummand(num_expr.first);

        for (const auto& [op, summand] : num_expr.rest) {
            checkSummand(summand);
        }
    }

    void checkSummand(const Summand& summand) {
        checkPrimary(summand.first);

        for (const auto& [op, primary] : summand.rest) {
            checkPrimary(primary);
        }
    }

    void checkPrimary(const Primary& primary) {
        std::visit(
            [this](const auto& prim) {
                using T = std::decay_t<decltype(prim)>;
                if constexpr (std::is_same_v<T, RoutineCall>) {
                    checkRoutineCall(prim);
                } else if constexpr (std::is_same_v<T, ModifiablePrimary>) {
                    checkModifiablePrimary(prim);
                } else if constexpr (std::is_same_v<T, std::unique_ptr<Expression>>) {
                    checkExpression(*prim);
                } else if constexpr (std::is_same_v<T, UnarySign>) {
                    checkPrimary(*prim.operand);
                }
            },
            primary);
    }

    void checkModifiablePrimary(const ModifiablePrimary& mp) {
        table.varExists(mp);
        table.markVarUsed(mp.variable);
        if (!mp.accessors.empty()) {
            std::reference_wrapper<const Type> current_type = table.getVariableType(mp);
            for (const auto& accessor : mp.accessors) {
                current_type = checkAccessor(mp, current_type.get(), accessor);
            }
        }
    }

    const Type& checkAccessor(const ModifiablePrimary& mp,
                              const Type& type_of_last,
                              const std::variant<Expression, std::string>& accessor) { // NOLINT(*complexity*)
        return std::visit(
            overloaded{
                [&](const std::string& field_name) -> const Type& { return checkField(mp, type_of_last, field_name); },
                [&](const Expression& index) -> const Type& { return checkIndex(mp, type_of_last, index); },
            },
            accessor);
    }

    const Type& checkField(const ModifiablePrimary& mp, const Type& type_of_last, const std::string& field_name) {
        const Type& resolved_type = table.resolveType(type_of_last);

        if (std::holds_alternative<ArrayType>(resolved_type)) {
            if (field_name == "size")
                return IntegerType{};
            throw SemanticError{"Variable " + mp.variable + " has no field named '" + field_name + "'", mp.span};
        }
        if (!std::holds_alternative<RecordType>(resolved_type))
            throw SemanticError{"Cannot access field '" + field_name + "' on non-record type", mp.span};

        for (const VariableDeclaration& field : std::get<RecordType>(resolved_type).fields) {
            if (field.identifier == field_name) {
                if (!field.type) {
                    // TODO: deduce type of the field here
                } else {
                    return *field.type;
                }
            }
        }
        throw SemanticError{mp.variable + " has no field '" + field_name + "'", mp.span};
    }

    const Type& checkIndex(const ModifiablePrimary& mp, const Type& type_of_last, const Expression& index) {
        const Type& resolved_type = table.resolveType(type_of_last);

        if (!std::holds_alternative<ArrayType>(resolved_type))
            throw SemanticError{"Cannot index non-array type", mp.span};

        checkExpression(index);
        const auto& array = std::get<ArrayType>(resolved_type);
        return *array.element_type;
    }

    void checkRoutineCall(const RoutineCall& call) {
        auto routine_it = table.getRoutines().find(call.name);
        if (routine_it == table.getRoutines().end()) {
            throw SemanticError{"Undeclared routine: " + call.name, call.span};
        }

        table.markRoutineUsed(call.name);
        const RoutineDeclaration& routine_decl = routine_it->second.declaration;

        if (call.arguments.size() != routine_decl.parameters.size()) {
            throw SemanticError{std::format("Routine {} expects {} arguments, but {} are provided",
                                            call.name,
                                            routine_decl.parameters.size(),
                                            call.arguments.size()),
                                call.span};
        }

        for (const Expression& arg : call.arguments) {
            checkExpression(arg);
        }
    }

    void checkType(const Type& type) {
        std::visit(overloaded{[this](const std::string& type_name) {
                                  table.typeExists(type_name);
                                  table.markTypeUsed(type_name);
                              },
                              [this](const ArrayType& array) {
                                  checkType(*array.element_type);
                                  if (array.size)
                                      checkExpression(*array.size);
                                  if (std::holds_alternative<std::string>(*array.element_type)) {
                                      const auto& type_name = std::get<std::string>(*array.element_type);
                                      table.markTypeUsed(type_name);
                                  }
                              },
                              [this](const RecordType& record) {
                                  for (const VariableDeclaration& field : record.fields) {
                                      if (field.type)
                                          checkType(*field.type);
                                      if (std::holds_alternative<std::string>(*field.type)) {
                                          const auto& type_name = std::get<std::string>(*field.type);
                                          table.markTypeUsed(type_name);
                                      }
                                  }
                              },
                              [](const auto&) {}},
                   type);
    }

    void checkBlock(const Block& block) {
        table.pushScope(block);
        for (const auto& element : block) {
            std::visit(overloaded{
                           [&](const VariableDeclaration& var) {
                               table.addLocalVariable(var);
                               if (var.type)
                                   checkType(*var.type);
                               if (var.value)
                                   checkExpression(*var.value);
                           },
                           [&](const TypeDeclaration& type) {
                               table.addLocalType(type);
                               checkType(type.type);
                           },
                           [&](const Statement& stmt) { checkStatement(stmt); },
                       },
                       element);
        }
        table.popScope(block);
    }

    void optimizeBlock(Block& block) { // NOLINT(*complexity*)
        auto block_usage_it = table.getScopes().find(&block);
        if (block_usage_it == table.getScopes().end())
            return;

        const std::unordered_map<std::string, VarInfo>& variable_usage = block_usage_it->second.variables;
        const std::unordered_map<std::string, TypeInfo>& type_usage = block_usage_it->second.types;

        Block optimized;
        for (bool found_return = false; auto& element : block) {
            if (found_return)
                break;
            std::visit(
                overloaded{
                    [&](VariableDeclaration& var) {
                        if ((variable_usage.contains(var.identifier) && variable_usage.at(var.identifier).used) ||
                            var.value) {
                            optimized.emplace_back(std::move(var));
                        }
                    },
                    [&](TypeDeclaration& type) {
                        if (type_usage.contains(type.identifier) && type_usage.at(type.identifier).used) {
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

    void checkStatement(const Statement& stmt) {
        std::visit(
            [this](const auto& statement) {
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
                    for (const auto& arg : statement.arguments) {
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

    void checkAssignment(const AssignmentStatement& assignment) {
        checkModifiablePrimary(assignment.target);
        checkExpression(assignment.expression);
        if (table.getForLoopVariables().contains(assignment.target.variable))
            throw SemanticError{"Cannot assign to for loop variable: " + assignment.target.variable, assignment.span};
    }

    void checkForStatement(const ForStatement& for_stmt) {
        table.pushScope(for_stmt.body);
        table.addLocalVariable(VariableDeclaration{
            for_stmt.counter, IntegerType{{0, 0, 0, 0}}, std::nullopt}); // TODO: add span to for counter
        table.markVarUsed(for_stmt.counter);
        table.getForLoopVariables().insert(for_stmt.counter);

        if (std::holds_alternative<Expression>(for_stmt.range)) {
            checkExpression(std::get<Expression>(for_stmt.range));
        } else {
            const auto& range = std::get<std::pair<Expression, Expression>>(for_stmt.range);
            checkExpression(range.first);
            checkExpression(range.second);
        }

        checkBlock(for_stmt.body);

        table.getForLoopVariables().erase(for_stmt.counter);
        table.popScope(for_stmt.body);
    }

    void checkRoutineDeclaration(RoutineDeclaration& routine) {
        for (const ParameterDeclaration& param : routine.parameters)
            checkType(param.type);

        if (!routine.body)
            return;

        if (auto* expr = std::get_if<Expression>(&*routine.body)) {
            checkExpression(*expr);
            Block block;
            block.emplace_back(ReturnStatement{.value = std::move(*expr)});
            *routine.body = std::move(block);
        }
        const Block& body = std::get<Block>(*routine.body);

        table.pushScope(body);
        for (const ParameterDeclaration& param : routine.parameters)
            table.addLocalParameter(param);
        if (routine.return_type)
            checkType(*routine.return_type);
        checkBlock(body);
        table.popScope(body);
    }

    void optimizeRoutine(RoutineDeclaration& routine) {
        if (!routine.body)
            return;

        if (std::holds_alternative<Block>(*routine.body)) {
            optimizeBlock(std::get<Block>(*routine.body));
        }
    }

    void checkForwardDeclarations() {
        for (const auto& [identifier, routine_info] : table.getRoutines()) {
            if (!routine_info.defined) {
                throw SemanticError{"Forward declared routine \"" + identifier + "\" is never defined",
                                    routine_info.declaration.get().span};
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
                if (!table.getGlobalScope().variables.at(var_decl.identifier).used) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<TypeDeclaration>(*it)) {
                const auto& type_decl = std::get<TypeDeclaration>(*it);
                if (!table.getGlobalScope().types.at(type_decl.identifier).used) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<RoutineDeclaration>(*it)) {
                auto& routine_decl = std::get<RoutineDeclaration>(*it);
                if (!table.getRoutines().at(routine_decl.identifier).used) {
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
                           [this](const VariableDeclaration& var) {
                               table.addLocalVariable(var);
                               if (var.type)
                                   checkType(*var.type);
                               if (var.value)
                                   checkExpression(*var.value);
                           },
                           [this](const TypeDeclaration& type) {
                               table.addLocalType(type);
                               checkType(type.type);
                           },
                           [this](RoutineDeclaration& routine) {
                               auto it = table.getRoutines().find(routine.identifier);
                               if (it != table.getRoutines().end()) {
                                   if (it->second.defined && routine.body)
                                       throw SemanticError{"Duplicate routine declaration: " + routine.identifier,
                                                           routine.span};
                                   if (routine.body)
                                       it->second.defined = true;
                               } else {
                                   bool is_defined = routine.body.has_value();
                                   table.getRoutines().emplace(
                                       routine.identifier,
                                       RoutineInfo{.declaration = routine, .defined = is_defined, .used = false});
                               }
                               checkRoutineDeclaration(routine);
                           },
                       },
                       decl);
        }
        checkForwardDeclarations();
    }

  public:
    explicit SemanticAnalyzer(Program& program, std::string_view entry_point)
        : program{program}, entry_point{entry_point}, table{} {}

    std::optional<SemanticError> analyze() {
        try {
            checkProgram();
        } catch (const SemanticError& error) {
            return error;
        }
        optimizeProgram(); // should never throw SemanticError
        return std::nullopt;
    }
};

std::optional<SemanticError> analyze(Program& ast, std::string_view entry_point) {
    SemanticAnalyzer analyzer{ast, entry_point};
    return analyzer.analyze();
}

} // namespace analyzer

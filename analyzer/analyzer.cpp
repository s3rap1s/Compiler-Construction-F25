#include "analyzer.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"
#include "utils.hpp"

#include <algorithm>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace analyzer {

using namespace parser;

struct LocalScope {
    std::unordered_map<std::string, std::pair<std::optional<Type>, bool>> variables;
    std::unordered_map<std::string, std::pair<Type, bool>> types;
};

struct SymbolTable {
    std::unordered_map<std::string, std::pair<std::pair<RoutineDeclaration, bool>, bool>> routines;

    std::vector<LocalScope> local_scopes;
    std::vector<std::string> for_loop_variables;

    std::unordered_map<const Block*, LocalScope> block_usage;
    const Block* current_block = nullptr;

    void pushScope() {
        local_scopes.emplace_back();
    }

    void popScope() {
        local_scopes.pop_back();
    }

    bool varExists(const ModifiablePrimary& mp) const {
        for (const auto& local_scope : std::ranges::reverse_view(local_scopes)) {
            if (local_scope.variables.contains(mp.variable)) {
                return true;
            }
        }
        throw SemanticError{"Undeclared variable: " + mp.variable, mp.span};
    }

    void addLocalVar(const VariableDeclaration& vd) {
        if (local_scopes.back().variables.contains(vd.identifier)) {
            throw SemanticError{"Duplicate variable declaration: " + vd.identifier, vd.span};
        }
        local_scopes.back().variables[vd.identifier] = {vd.type, false};
    }

    void markVarUsed(const std::string& identifier) {
        for (auto& local_scope : std::ranges::reverse_view(local_scopes)) {
            if (local_scope.variables.contains(identifier)) {
                local_scope.variables[identifier].second = true;
                if (current_block) {
                    block_usage[current_block].variables[identifier].second = true;
                }
                return;
            }
        }
    }

    bool typeExists(const Type& type) const {
        if (std::holds_alternative<std::string>(type)) {
            const auto& type_str = std::get<std::string>(type);
            for (const auto& local_scope : std::ranges::reverse_view(local_scopes)) {
                if (local_scope.types.contains(type_str)) {
                    return true;
                }
            }
            throw SemanticError{"Undeclared type: " + type_str, {}}; // TODO: add span to type
        }
        return true;
    }

    Type getVariableType(const ModifiablePrimary& mp) {
        for (const auto& local_scope : std::ranges::reverse_view(local_scopes)) {
            if (local_scope.variables.contains(mp.variable) && local_scope.variables.at(mp.variable).first) {
                return *local_scope.variables.at(mp.variable).first;
            }
        }
        throw SemanticError{"Undeclared variablee: " + mp.variable, mp.span};
    }

    Type resolveType(Type type) {
        while (std::holds_alternative<std::string>(type)) {
            const std::string& type_name = std::get<std::string>(type);
            bool found = false;

            for (const auto& local_scope : std::ranges::reverse_view(local_scopes)) {
                if (local_scope.types.contains(type_name)) {
                    type = local_scope.types.at(type_name).first;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw SemanticError{"Undeclared type: " + type_name, {}};
            }
        }
        return type;
    }

    void addLocalType(const TypeDeclaration& td) {
        if (local_scopes.back().types.contains(td.identifier)) {
            throw SemanticError{"Duplicate type declaration: " + td.identifier, td.span};
        }
        local_scopes.back().types[td.identifier] = {td.type, false};
    }

    void markTypeUsed(const std::string& identifier) {
        for (auto& local_scope : std::ranges::reverse_view(local_scopes)) {
            if (local_scope.types.contains(identifier)) {
                local_scope.types[identifier].second = true;
                if (current_block) {
                    block_usage[current_block].types[identifier].second = true;
                }
                return;
            }
        }
    }

    void markRoutineUsed(const std::string& identifier) {
        if (routines.contains(identifier)) {
            routines[identifier].second = true;
        }
    }

    void setCurrentBlock(const Block* ptr) {
        current_block = ptr;
        if (ptr && !block_usage.contains(ptr)) {
            block_usage[ptr] = {};
        }
    }

    void saveBlockUsage(const Block& block) {
        auto& current_scope = local_scopes.back();
        auto& block_usage_cur = block_usage[&block];

        for (const auto& [var, used] : current_scope.variables) {
            block_usage_cur.variables[var] = used;
        }
        for (const auto& [type, used] : current_scope.types) {
            block_usage_cur.types[type] = used;
        }
    }
};

class SemanticAnalyzer {
  private:
    Program& program; // NOLINT(*ref*)
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
                } else if constexpr (std::is_same_v<T, std::shared_ptr<Expression>>) {
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
            auto current_type = table.getVariableType(mp);
            for (const auto& accessor : mp.accessors) {
                current_type = checkAccessor(mp, current_type, accessor);
            }
        }
    }

    Type checkAccessor(const ModifiablePrimary& mp,
                       Type current_type,
                       const std::variant<Expression, std::string>& accessor) { // NOLINT(*complexity*)
        current_type = table.resolveType(current_type);
        if (std::holds_alternative<std::string>(accessor)) {
            const auto& field_name = std::get<std::string>(accessor);

            if (std::holds_alternative<ArrayType>(current_type)) {
                if (field_name == "size") {
                    return IntegerType{};
                }
                throw SemanticError{"Variable " + mp.variable + " has no field named '" + field_name + "'", mp.span};
            }
            if (!std::holds_alternative<RecordType>(current_type)) {
                throw SemanticError{"Cannot access field '" + field_name + "' on non-record type", mp.span};
            }

            const auto& record = std::get<RecordType>(current_type);
            bool field_found = false;

            for (const auto& field : record.fields) {
                if (field->identifier == field_name) {
                    if (!field->type) {
                        // TODO: deduce type of the field here
                    } else {
                        field_found = true;
                        return *field->type;
                    }
                }
            }

            if (!field_found) {
                throw SemanticError{mp.variable + " has no field '" + field_name + "'", mp.span};
            }
        } else {
            if (!std::holds_alternative<ArrayType>(current_type)) {
                throw SemanticError{"Cannot index non-array type", mp.span};
            }

            const auto& index_expr = std::get<Expression>(accessor);
            checkExpression(index_expr);
            current_type = table.resolveType(current_type);
            const auto& array = std::get<ArrayType>(current_type);
            return *array.element_type;
        }

        throw SemanticError{"Invalid accessor", mp.span};
    }

    void checkRoutineCall(const RoutineCall& call) {
        auto routine_it = table.routines.find(call.name);
        if (routine_it == table.routines.end()) {
            throw SemanticError{"Undeclared routine: " + call.name, call.span};
        }
        table.markRoutineUsed(call.name);
        const RoutineDeclaration& routine_decl = routine_it->second.first.first;

        if (call.arguments.size() != routine_decl.parameters.size()) {
            throw SemanticError{"Routine " + call.name + " expects " + std::to_string(routine_decl.parameters.size()) +
                                    " arguments, but " + std::to_string(call.arguments.size()) + " provided",
                                call.span};
        }

        for (const auto& arg : call.arguments) {
            checkExpression(arg);
        }
    }

    void checkType(const Type& type) {
        std::visit(
            [this](const auto& t) {
                using T = std::decay_t<decltype(t)>;

                if constexpr (std::is_same_v<T, std::string>) {
                    table.typeExists(t);
                    table.markTypeUsed(t);
                } else if constexpr (std::is_same_v<T, ArrayType>) {
                    checkType(*t.element_type);
                    if (t.size) {
                        checkExpression(*t.size);
                    }
                    if (std::holds_alternative<std::string>(*t.element_type)) {
                        const auto& type_name = std::get<std::string>(*t.element_type);
                        table.markTypeUsed(type_name);
                    }
                } else if constexpr (std::is_same_v<T, RecordType>) {
                    for (const auto& field : t.fields) {
                        if (field->type) {
                            checkType(*field->type);
                        }
                        if (std::holds_alternative<std::string>(*field->type)) {
                            const auto& type_name = std::get<std::string>(*field->type);
                            table.markTypeUsed(type_name);
                        }
                    }
                }
            },
            type);
    }

    void checkBlock(const Block& block) {
        table.pushScope();
        table.setCurrentBlock(&block);
        for (const auto& element : block) {
            std::visit(
                [this](const auto& elem) {
                    using T = std::decay_t<decltype(elem)>;

                    if constexpr (std::is_same_v<T, VariableDeclaration>) {
                        table.addLocalVar(elem);

                        if (elem.type) {
                            checkType(*elem.type);
                        }
                        if (elem.value) {
                            checkExpression(*elem.value);
                        }
                    } else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                        table.addLocalType(elem);
                        checkType(elem.type);
                    } else if constexpr (std::is_same_v<T, Statement>) {
                        checkStatement(elem);
                    }
                },
                element);
        }
        table.saveBlockUsage(block);
        table.popScope();
    }

    Block optimizeBlock(Block& block) { // NOLINT(*complexity*)
        Block optimized;
        bool found_return = false;

        auto block_usage_it = table.block_usage.find(&block);
        if (block_usage_it == table.block_usage.end()) {
            return block;
        }

        const auto& variable_usage = block_usage_it->second.variables;
        const auto& type_usage = block_usage_it->second.types;

        for (auto& element : block) {
            if (found_return) {
                break;
            }
            std::visit(
                [&](auto& elem) {
                    using T = std::decay_t<decltype(elem)>;

                    if constexpr (std::is_same_v<T, VariableDeclaration>) {
                        if ((variable_usage.contains(elem.identifier) && variable_usage.at(elem.identifier).second) ||
                            elem.value) {
                        }
                    } else if constexpr (std::is_same_v<T, Statement>) {
                        auto optimized_statement = optimizeStatement(elem);
                        if (optimized_statement) {
                            optimized.push_back(*optimized_statement);

                            if (std::holds_alternative<ReturnStatement>(*optimized_statement)) {
                                found_return = true;
                            }
                        }
                    } else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                        if (type_usage.contains(elem.identifier) && type_usage.at(elem.identifier).second) {
                            optimized.push_back(elem);
                        }
                    }
                },
                element);
        }

        return optimized;
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

    std::optional<Statement> optimizeStatement(Statement& stmt) {
        return std::visit(
            [&](auto& statement) -> std::optional<Statement> {
                using T = std::decay_t<decltype(statement)>;

                if constexpr (std::is_same_v<T, WhileStatement>) {
                    auto optimized_body = optimizeBlock(statement.body);
                    return WhileStatement{{statement.span}, statement.condition, optimized_body};
                } else if constexpr (std::is_same_v<T, ForStatement>) {
                    auto optimized_body = optimizeBlock(statement.body);
                    return ForStatement{
                        {statement.span}, statement.counter, statement.range, optimized_body, statement.is_reversed};
                } else if constexpr (std::is_same_v<T, IfStatement>) {
                    auto optimized_then = optimizeBlock(statement.true_branch);
                    std::optional<Block> optimized_else;
                    if (statement.false_branch) {
                        optimized_else = optimizeBlock(*statement.false_branch);
                    }
                    return IfStatement{{statement.span}, statement.condition, optimized_then, optimized_else};
                } else {
                    return statement;
                }
            },
            stmt);
    }

    void checkAssignment(const AssignmentStatement& assignment) {
        checkModifiablePrimary(assignment.target);

        if (std::ranges::find(table.for_loop_variables, assignment.target.variable) != table.for_loop_variables.end()) {
            throw SemanticError{"Cannot assign to for loop variable: " + assignment.target.variable, assignment.span};
        }

        checkExpression(assignment.expression);
    }

    void checkForStatement(const ForStatement& for_stmt) {
        table.pushScope();
        table.addLocalVar(VariableDeclaration{
            for_stmt.counter, IntegerType{{0, 0, 0, 0}}, std::nullopt}); // TODO: add span to for counter
        table.markVarUsed(for_stmt.counter);
        table.for_loop_variables.push_back(for_stmt.counter);

        if (std::holds_alternative<Expression>(for_stmt.range)) {
            checkExpression(std::get<Expression>(for_stmt.range));
        } else {
            const auto& range = std::get<std::pair<Expression, Expression>>(for_stmt.range);
            checkExpression(range.first);
            checkExpression(range.second);
        }

        checkBlock(for_stmt.body);

        table.for_loop_variables.pop_back();
        table.popScope();
    }

    void checkRoutineDeclaration(const RoutineDeclaration& routine) {
        if (!routine.body)
            return;

        table.pushScope();

        for (const auto& param : routine.parameters) {
            table.addLocalVar(VariableDeclaration{param.identifier, param.type, std::nullopt});
            checkType(param.type);
        }
        if (routine.return_type) {
            checkType(*routine.return_type);
        }
        if (std::holds_alternative<Expression>(*routine.body)) {
            checkExpression(std::get<Expression>(*routine.body));
        } else {
            checkBlock(std::get<Block>(*routine.body));
        }

        table.popScope();
    }

    void optimizeRoutine(RoutineDeclaration& routine) {
        if (!routine.body)
            return;

        if (std::holds_alternative<Block>(*routine.body)) {
            auto& block = std::get<Block>(*routine.body);
            auto optimized_block = optimizeBlock(block);
            routine.body = optimized_block;
        }
    }

    void checkForwardDeclarations() {
        for (const auto& [identifier, routine_pair] : table.routines) {
            if (!routine_pair.first.second) {
                throw SemanticError{"Forward declared routine \"" + identifier + "\" is never defined",
                                    routine_pair.first.first.span};
            }
        }
    }

    void optimizeProgram() {
        table.routines.at(program.entry_point.identifier).second = true;
        auto it = program.declarations.begin();
        while (it != program.declarations.end()) {
            if (std::holds_alternative<VariableDeclaration>(*it)) {
                const auto& var_decl = std::get<VariableDeclaration>(*it);
                if (!table.local_scopes.front().variables.at(var_decl.identifier).second) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<TypeDeclaration>(*it)) {
                const auto& type_decl = std::get<TypeDeclaration>(*it);
                if (!table.local_scopes.front().types.at(type_decl.identifier).second) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<RoutineDeclaration>(*it)) {
                auto& routine_decl = std::get<RoutineDeclaration>(*it);
                if (!table.routines.at(routine_decl.identifier).second) {
                    it = program.declarations.erase(it);
                    continue;
                }
                optimizeRoutine(routine_decl);
            }
            ++it;
        }
    }

    std::optional<SemanticError> checkProgram() { // NOLINT(*complexity*)
        try {
            table.pushScope();

            for (const auto& decl : program.declarations) {
                std::visit(
                    [this](const auto& d) {
                        using T = std::decay_t<decltype(d)>;

                        if constexpr (std::is_same_v<T, VariableDeclaration>) {
                            table.addLocalVar(d);
                            if (d.type) {
                                checkType(*d.type);
                            }
                            if (d.value) {
                                checkExpression(*d.value);
                            }
                        } else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                            table.addLocalType(d);
                            checkType(d.type);
                        } else if constexpr (std::is_same_v<T, RoutineDeclaration>) {
                            auto it = table.routines.find(d.identifier);
                            if (it != table.routines.end()) {
                                if (it->second.first.second && d.body) {
                                    throw SemanticError{"Duplicate routine declaration: " + d.identifier, d.span};
                                }
                                if (d.body) {
                                    it->second.first.second = true;
                                }
                            } else {
                                bool is_defined = static_cast<bool>(d.body);
                                table.routines[d.identifier] = {{d, is_defined}, false};
                            }
                            checkRoutineDeclaration(d);
                        }
                    },
                    decl);
            }

            checkForwardDeclarations();

        } catch (const SemanticError& error) {
            return error;
        }
        return std::nullopt;
    }

  public:
    explicit SemanticAnalyzer(Program& program) : program{program} {
        table = SymbolTable{};
    }

    std::expected<Program, SemanticError> analyze() {
        auto error = checkProgram();
        if (error) {
            return std::unexpected{*error};
        }
        optimizeProgram();
        return program;
    }
};

std::expected<Program, SemanticError> analyze(Program& ast) {
    SemanticAnalyzer analyzer{ast};
    return analyzer.analyze();
}

} // namespace analyzer


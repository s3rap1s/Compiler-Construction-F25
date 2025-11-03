#include "analyzer.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"

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

class SemanticAnalyzer {
private:
    class SymbolTable{
        public:
        std::unordered_map<std::string, bool> variables;
        std::unordered_map<std::string, bool> types;
        std::unordered_map<std::string, std::pair<std::pair<RoutineDeclaration, bool>, bool>> routines;
        
        std::vector<std::unordered_map<std::string, bool>> local_scopes;
        std::vector<std::string> for_loop_variables;

        std::unordered_map<const Block*, std::unordered_map<std::string, bool>> block_var_usage;
        const Block* current_block = nullptr;
        
        
        void pushScope() {
            local_scopes.emplace_back();
        }
        
        void popScope() {
            local_scopes.pop_back();
        }
        
        bool varExists(const std::string& identifier) const {
            for (const auto& local_scope : std::ranges::reverse_view(local_scopes)) {
                if (local_scope.contains(identifier)) {
                    return true;
                }
            }
            return variables.contains(identifier);
        }
        
        void addLocalVar(const std::string& identifier) {
            if (!local_scopes.empty()) {
                local_scopes.back()[identifier] = false;
            }
        }

        void markVarUsed(const std::string& identifier){
            for (auto& local_scope: std::ranges::reverse_view(local_scopes)){
                if (local_scope.contains(identifier)) {
                    local_scope[identifier] = true;
                    if (current_block){
                        block_var_usage[current_block][identifier] = true;
                    }
                    return;
                }
            }
            if (variables.contains(identifier)){
                variables[identifier] = true;
            }
        }

        void markTypeUsed(const std::string& identifier) {
            if (types.contains(identifier)) {
                types[identifier] = true;
            }
        }

        void markRoutineUsed(const std::string& identifier){
            if (routines.contains(identifier)){
                routines[identifier].second = true;
            }
        }

        void setCurrentBlock(const Block* ptr){
            current_block = ptr;
            if (ptr && !block_var_usage.contains(ptr)){
                block_var_usage[ptr] = {};
            }
        }

        void saveBlockVarUsage(const Block& block) {
            if (!local_scopes.empty()) {
                auto& current_scope = local_scopes.back();
                auto& block_usage = block_var_usage[&block];
                
                for (const auto& [var_name, used] : current_scope) {
                    block_usage[var_name] = used;
                }
            }
        }
    }; 

    Program& program; // NOLINT(*ref*)
    SymbolTable table;

    void checkExpression(const Expression& expr) {
        checkBooleanExpression(expr.first);
        
        for (const auto& [op, bool_expr] : expr.rest) {
            checkBooleanExpression(bool_expr);
        }
    }

    void checkBooleanExpression(const BooleanExpression& bool_expr) {
        std::visit([this](const auto& expr) {
            using T = std::decay_t<decltype(expr)>;
            
            if constexpr (std::is_same_v<T, Relation>) {
                checkNumberExpression(expr.first);
                if (expr.second) {
                    checkNumberExpression(expr.second->second);
                }
            } else if constexpr (std::is_same_v<T, NotExpression>) {
                checkPrimary(expr.operand);
            }
        }, bool_expr);
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
        std::visit([this](const auto& prim) {
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
        }, primary);
    }

    void checkModifiablePrimary(const ModifiablePrimary& mp) {
        if (!table.varExists(mp.variable)) {
            throw SemanticError{"Undeclared variable: " + mp.variable, mp.span};
        }
        table.markVarUsed(mp.variable);        

        for (const auto& accessor : mp.accessors) {
            if (std::holds_alternative<Expression>(accessor)) {
                checkExpression(std::get<Expression>(accessor));
            }
        }
    }

    void checkRoutineCall(const RoutineCall& call) {
        auto routine_it = table.routines.find(call.name);
        if (routine_it == table.routines.end()) {
            throw SemanticError{"Undeclared routine: " + call.name, call.span};
        }
        table.markRoutineUsed(call.name);
        const RoutineDeclaration& routine_decl = routine_it->second.first.first;
        
        if (call.arguments.size() != routine_decl.parameters.size()) {
            throw SemanticError{"Routine " + call.name + " expects " + 
                    std::to_string(routine_decl.parameters.size()) + 
                    " arguments, but " + std::to_string(call.arguments.size()) + " provided", call.span};
        }
        
        for (const auto& arg : call.arguments) {
            checkExpression(arg);
        }
    }

    void checkType(const Type& type) {
        std::visit([this](const auto& t) {
            using T = std::decay_t<decltype(t)>;
            
            if constexpr (std::is_same_v<T, std::string>) {
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
        }, type);
    }

    void checkBlock(const Block& block) {
        table.pushScope();
        table.setCurrentBlock(&block);
        for (const auto& element : block) {
            std::visit([this](const auto& elem) {
                using T = std::decay_t<decltype(elem)>;
                
                if constexpr (std::is_same_v<T, VariableDeclaration>) {
                    table.addLocalVar(elem.identifier);
                    
                    if (elem.type) {
                        checkType(*elem.type);
                    }
                    if (elem.value) {
                        checkExpression(*elem.value);
                    }
                } else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                    checkType(elem.type);
                } else if constexpr (std::is_same_v<T, Statement>) {
                    checkStatement(elem);
                }
            }, element);
        }
        table.saveBlockVarUsage(block);
        table.popScope();
    }

    Block optimizeBlock(Block& block) { //NOLINT(*complexity*)
        Block optimized;
        bool found_return = false;
        
        auto block_usage_it = table.block_var_usage.find(&block);
        if (block_usage_it == table.block_var_usage.end()) {
            return block;
        }
        
        const auto& variable_usage = block_usage_it->second;
        
        for (auto& element : block) {
            if (found_return) {
                continue;
            }
            
            std::visit([&](auto& elem) {
                using T = std::decay_t<decltype(elem)>;
                
                if constexpr (std::is_same_v<T, VariableDeclaration>) {
                    if (variable_usage.contains(elem.identifier) && variable_usage.at(elem.identifier)) { // probably should include || elem.value, cuz Expression in value can be impure
                        optimized.push_back(elem);
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
                    if (table.types.contains(elem.identifier) && table.types.at(elem.identifier)) {
                        optimized.push_back(elem);
                    }
                }
            }, element);
        }
        
        return optimized;
    }

    void checkStatement(const Statement& stmt) {
        std::visit([this](const auto& statement) {
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
        }, stmt);
    }

    std::optional<Statement> optimizeStatement(Statement& stmt) {
        return std::visit([&](auto& statement) -> std::optional<Statement> {
            using T = std::decay_t<decltype(statement)>;
            
            if constexpr (std::is_same_v<T, WhileStatement>) {
                auto optimized_body = optimizeBlock(statement.body);
                return WhileStatement{{statement.span}, statement.condition, optimized_body};
            } else if constexpr (std::is_same_v<T, ForStatement>) {
                auto optimized_body = optimizeBlock(statement.body);
                return ForStatement{{statement.span}, statement.counter, statement.range, optimized_body, statement.is_reversed};
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
        }, stmt);
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
        table.addLocalVar(for_stmt.counter);
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
        if (!routine.body) return;
        
        table.pushScope();
        
        for (const auto& param : routine.parameters) {
            table.addLocalVar(param.identifier);
            checkType(param.type);
        }
        if (routine.return_type){
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
        if (!routine.body) return;
        
        if (std::holds_alternative<Block>(*routine.body)) {
            auto& block = std::get<Block>(*routine.body);
            auto optimized_block = optimizeBlock(block);
            routine.body = optimized_block;
        }
    }
    
    void checkForwardDeclarations() {
        for (const auto& [identifier, routine_pair] : table.routines) {
            if (!routine_pair.first.second) {
                throw SemanticError{"Forward declared routine \"" + identifier + "\" is never defined", routine_pair.first.first.span};
            }
        }
    }

    void optimizeProgram() {
        table.routines.at(program.entry_point.identifier).second = true;
        auto it = program.declarations.begin();
        while (it != program.declarations.end()) {
            if (std::holds_alternative<VariableDeclaration>(*it)) {
                const auto& var_decl = std::get<VariableDeclaration>(*it);
                if (!table.variables.at(var_decl.identifier)) {
                    it = program.declarations.erase(it);
                    continue;
                }
            }
            if (std::holds_alternative<TypeDeclaration>(*it)) {
                const auto& type_decl = std::get<TypeDeclaration>(*it);
                if (!table.types.at(type_decl.identifier)) {
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

    std::optional<SemanticError> checkProgram() { //NOLINT(*complexity*)
        try {
            table.pushScope();
            
            for (const auto& decl : program.declarations) {
                std::visit([this](const auto& d) {
                    using T = std::decay_t<decltype(d)>;
                    
                    if constexpr (std::is_same_v<T, VariableDeclaration>) {
                        if (table.variables.find(d.identifier) != table.variables.end()) {
                            throw SemanticError{"Duplicate variable declaration: " + d.identifier, d.span};
                        }
                        table.variables[d.identifier] = false;
                        table.addLocalVar(d.identifier);
                        if (d.type){
                            checkType(*d.type);
                        }
                        if (d.value){
                            checkExpression(*d.value);
                        }
                    } else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                        if (table.types.contains(d.identifier)) {
                            throw SemanticError{"Duplicate type declaration: " + d.identifier, d.span};
                        }
                        table.types[d.identifier] = false;
                        checkType(d.type);
                    } else if constexpr (std::is_same_v<T, RoutineDeclaration>) {
                        auto it = table.routines.find(d.identifier);
                        if (it != table.routines.end()) {
                            if (it->second.first.second && d.body){
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
                }, decl);
            }

            checkForwardDeclarations();
            
            table.popScope();
            
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
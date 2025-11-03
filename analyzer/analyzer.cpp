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
        std::unordered_map<std::string, VariableDeclaration> variables;
        std::unordered_map<std::string, TypeDeclaration> types;
        std::unordered_map<std::string, std::pair<RoutineDeclaration, bool>> routines;
        
        std::vector<std::unordered_map<std::string, VariableDeclaration>> local_scopes;
        std::vector<std::string> for_loop_variables;

        
        void pushScope() {
            local_scopes.emplace_back();
        }
        
        void popScope() {
            local_scopes.pop_back();
        }
        
        bool varExists(const std::string& identifier) const {
            for (const auto & local_scope : std::ranges::reverse_view(local_scopes)) {
                if (local_scope.contains(identifier)) {
                    return true;
                }
            }
            return variables.contains(identifier);
        }
        
        void addLocalVar(const VariableDeclaration& var) {
            if (!local_scopes.empty()) {
                local_scopes.back()[var.identifier] = var;
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
            }
            else if constexpr (std::is_same_v<T, NotExpression>) {
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
            }
            else if constexpr (std::is_same_v<T, ModifiablePrimary>) {
                checkModifiablePrimary(prim);
            }
            else if constexpr (std::is_same_v<T, std::shared_ptr<Expression>>) {
                checkExpression(*prim);
            }
            else if constexpr (std::is_same_v<T, UnarySign>) {
                checkPrimary(*prim.operand);
            }
        }, primary);
    }

    void checkModifiablePrimary(const ModifiablePrimary& mp) {
        if (!table.varExists(mp.variable)) {
            throw SemanticError{"Undeclared variable: " + mp.variable, mp.span};
        }
        
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
        
        const auto& routine_decl = routine_it->second.first;
        
        if (call.arguments.size() != routine_decl.parameters.size()) {
            throw SemanticError{"Routine " + call.name + " expects " + 
                    std::to_string(routine_decl.parameters.size()) + 
                    " arguments, but " + std::to_string(call.arguments.size()) + " provided", call.span};
        }
        
        for (const auto& arg : call.arguments) {
            checkExpression(arg);
        }
    }

    void checkBlock(const Block& block) {
        table.pushScope();
        
        for (const auto& element : block) {
            std::visit([this](const auto& elem) {
                using T = std::decay_t<decltype(elem)>;
                
                if constexpr (std::is_same_v<T, VariableDeclaration>) {
                    table.addLocalVar(elem);
                    
                    if (elem.value) {
                        checkExpression(*elem.value);
                    }
                }
                else if constexpr (std::is_same_v<T, Statement>) {
                    checkStatement(elem);
                }
            }, element);
        }
        
        table.popScope();
    }

    void checkStatement(const Statement& stmt) {
        std::visit([this](const auto& statement) {
            using T = std::decay_t<decltype(statement)>;
            
            if constexpr (std::is_same_v<T, AssignmentStatement>) {
                checkAssignment(statement);
            }
            else if constexpr (std::is_same_v<T, WhileStatement>) {
                checkExpression(statement.condition);
                checkBlock(statement.body);
            }
            else if constexpr (std::is_same_v<T, ForStatement>) {
                checkForStatement(statement);
            }
            else if constexpr (std::is_same_v<T, IfStatement>) {
                checkExpression(statement.condition);
                checkBlock(statement.true_branch);
                if (statement.false_branch) {
                    checkBlock(*statement.false_branch);
                }
            }
            else if constexpr (std::is_same_v<T, PrintStatement>) {
                for (const auto& arg : statement.arguments) {
                    if (std::holds_alternative<Expression>(arg)) {
                        checkExpression(std::get<Expression>(arg));
                    }
                }
            }
            else if constexpr (std::is_same_v<T, ReturnStatement>) {
                if (statement.value) {
                    checkExpression(*statement.value);
                }
            }
            else if constexpr (std::is_same_v<T, RoutineCall>) {
                checkRoutineCall(statement);
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
        VariableDeclaration loop_var{for_stmt.counter, std::nullopt, std::nullopt};
        table.addLocalVar(loop_var);
        
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
            VariableDeclaration param_var{param.identifier, param.type, std::nullopt};
            table.addLocalVar(param_var);
        }
        
        if (std::holds_alternative<Expression>(*routine.body)) {
            checkExpression(std::get<Expression>(*routine.body));
        } else {
            checkBlock(std::get<Block>(*routine.body));
        }
        
        table.popScope();
    }

    void checkForwardDeclarations() {
        for (const auto& [identifier, routine_pair] : table.routines) {
            if (!routine_pair.second) {
                throw SemanticError{"Forward declared routine \"" + identifier + "\" is never defined", routine_pair.first.span};
            }
        }
    }

public:
    explicit SemanticAnalyzer(Program& program) : program{program} {
        table = SymbolTable{};
    }

    std::optional<SemanticError> analysisChecks() { //NOLINT(*complexity*)
        try {
            table.pushScope();
            
            for (const auto& decl : program.declarations) {
                std::visit([this](const auto& d) {
                    using T = std::decay_t<decltype(d)>;
                    
                    if constexpr (std::is_same_v<T, VariableDeclaration>) {
                        if (table.variables.find(d.identifier) != table.variables.end()) {
                            throw SemanticError{"Duplicate variable declaration: " + d.identifier, d.span};
                        }
                        table.variables[d.identifier] = d;
                        table.addLocalVar(d);
                    }
                    else if constexpr (std::is_same_v<T, TypeDeclaration>) {
                        if (table.types.contains(d.identifier)) {
                            throw SemanticError{"Duplicate type declaration: " + d.identifier, d.span};
                        }
                        table.types[d.identifier] = d;
                    }
                    else if constexpr (std::is_same_v<T, RoutineDeclaration>) {
                        auto it = table.routines.find(d.identifier);
                        if (it != table.routines.end() && it->second.second) {
                            throw SemanticError{"Duplicate routine declaration: " + d.identifier, d.span};
                        }
                        bool is_defined = static_cast<bool>(d.body);
                        table.routines[d.identifier] = {d, is_defined};
                    }
                }, decl);
            }

            for (const auto& decl : program.declarations) {
                if (std::holds_alternative<RoutineDeclaration>(decl)) {
                    const auto& routine = std::get<RoutineDeclaration>(decl);
                    if (routine.body) {
                        checkRoutineDeclaration(routine);
                    }
                }
            }

            checkForwardDeclarations();
            
            table.popScope();
            
        } catch (const SemanticError& error) {
            return error;
        }
        return std::nullopt;
    }

    std::expected<Program, SemanticError> analyzeProgram() {
        auto error = analysisChecks();
        if (error) {
            return std::unexpected{*error};
        }
        return program;
    }
};

std::expected<Program, SemanticError> analyze(Program& ast) {
    SemanticAnalyzer analyzer{ast};
    return analyzer.analyzeProgram();
}

} // namespace analyzer
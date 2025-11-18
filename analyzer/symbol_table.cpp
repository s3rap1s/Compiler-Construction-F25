#include "symbol_table.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace analyzer {

using namespace parser;

std::unordered_map<std::string, RoutineInfo>& SymbolTable::getRoutines() {
    return routines;
}

std::unordered_map<const Block*, Scope>& SymbolTable::getScopes() {
    return scopes;
}

std::unordered_set<std::string>& SymbolTable::getForLoopVariables() {
    return for_loop_variables;
}

SymbolTable::ScopesView<false> SymbolTable::getScopesView() {
    return ScopesView<false>{this};
}

SymbolTable::ScopesView<true> SymbolTable::getScopesView() const {
    return ScopesView<true>{this};
}

Scope& SymbolTable::getGlobalScope() {
    return scopes.find(nullptr)->second;
}

Scope& SymbolTable::getCurrentScope() {
    return scopes.find(current_block)->second;
}

void SymbolTable::pushScope(const Block& block) {
    if (current_block == &block)
        return;
    scopes.try_emplace(&block, current_block);
    current_block = &block;
}

void SymbolTable::popScope(const Block& block) {
    if (current_block == &block)
        current_block = scopes.find(current_block)->second.parent;
}

bool SymbolTable::varExists(const ModifiablePrimary& mp) const {
    for (const Scope& scope : getScopesView()) {
        if (scope.variables.contains(mp.variable))
            return true;
    }
    throw SemanticError{"Undeclared variable: " + mp.variable, mp.span};
}

bool SymbolTable::typeExists(const std::string& type_name) const {
    for (const Scope& scope : getScopesView()) {
        if (scope.types.contains(type_name))
            return true;
    }
    throw SemanticError{"Undeclared type: " + type_name, {}}; // TODO: add span to type
}

bool SymbolTable::typeExists(const Type& type) const {
    if (std::holds_alternative<std::string>(type))
        return typeExists(std::get<std::string>(type));
    return true;
}

void SymbolTable::addLocalVariable(const VariableDeclaration& vd) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.variables.contains(vd.identifier))
        throw SemanticError{"Duplicate variable declaration: " + vd.identifier, vd.span};
    last_scope.variables.emplace(vd.identifier, VarInfo{.type = vd.type ? &*vd.type : nullptr, .used = false});
}

void SymbolTable::addLocalParameter(const ParameterDeclaration& pd) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.variables.contains(pd.identifier))
        throw SemanticError{"Duplicate variable declaration: " + pd.identifier, pd.span};
    last_scope.variables.emplace(pd.identifier, VarInfo{.type = &pd.type, .used = false});
}

void SymbolTable::addLocalType(const TypeDeclaration& td) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.types.contains(td.identifier))
        throw SemanticError{"Duplicate type declaration: " + td.identifier, td.span};
    last_scope.types.emplace(td.identifier, TypeInfo{.type = td.type, .used = false});
}

const Type& SymbolTable::getVariableType(const ModifiablePrimary& mp) const {
    for (const Scope& scope : getScopesView()) {
        if (auto it = scope.variables.find(mp.variable); it != scope.variables.end() && it->second.type != nullptr) {
            return *it->second.type;
        }
    }
    throw SemanticError{"Undeclared variable: " + mp.variable, mp.span};
}

const Type& SymbolTable::resolveType(const Type& type) const {
    if (!std::holds_alternative<std::string>(type))
        return type;

    const auto& type_name = std::get<std::string>(type);
    for (const Scope& local_scope : getScopesView()) {
        if (auto it = local_scope.types.find(type_name); it != local_scope.types.end())
            return it->second.type;
    }

    throw SemanticError{"Undeclared type: " + type_name, {}};
}

void SymbolTable::markVarUsed(const std::string& identifier) {
    for (Scope& scope : getScopesView()) {
        if (scope.variables.contains(identifier)) {
            scope.variables.at(identifier).used = true;
            return;
        }
    }
}

void SymbolTable::markTypeUsed(const std::string& identifier) {
    for (Scope& scope : getScopesView()) {
        if (scope.types.contains(identifier)) {
            scope.types.at(identifier).used = true;
            return;
        }
    }
}

void SymbolTable::markRoutineUsed(const std::string& identifier) {
    if (auto it = routines.find(identifier); it != routines.end())
        it->second.used = true;
}

} // namespace analyzer

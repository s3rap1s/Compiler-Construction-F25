#include "symbol_table.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/ast.hpp"

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
        if (scope.variables.contains(mp.variable.text))
            return true;
    }
    throw SemanticError{"Undeclared variable: " + mp.variable.text, mp.variable.span};
}

bool SymbolTable::typeExists(const Identifier& type_name) const {
    for (const Scope& scope : getScopesView()) {
        if (scope.types.contains(type_name.text))
            return true;
    }
    throw SemanticError{"Undeclared type: " + type_name.text, type_name.span};
}

bool SymbolTable::typeExists(const Type& type) const {
    if (std::holds_alternative<Identifier>(type))
        return typeExists(std::get<Identifier>(type));
    return true;
}

void SymbolTable::addLocalVariable(Identifier name, const Type* type) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.variables.contains(name.text))
        throw SemanticError{"Duplicate variable declaration: " + name.text, name.span};
    last_scope.variables.emplace(std::move(name.text), VarInfo{.type = type, .used = false});
}

void SymbolTable::addLocalType(Identifier name, const Type& type) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.types.contains(name.text))
        throw SemanticError{"Duplicate type declaration: " + name.text, name.span};
    last_scope.types.emplace(std::move(name.text), TypeInfo{.type = type, .used = false});
}

const Type& SymbolTable::getVariableType(const Identifier& name) const {
    for (const Scope& scope : getScopesView()) {
        if (auto it = scope.variables.find(name.text);
            it != scope.variables.end() && it->second.type != nullptr) {
            return *it->second.type;
        }
    }
    throw SemanticError{"Undeclared variable: " + name.text, name.span};
}

const Type& SymbolTable::resolveType(const Type& type) const {
    if (!std::holds_alternative<Identifier>(type))
        return type;

    const auto& type_name = std::get<Identifier>(type);
    for (const Scope& local_scope : getScopesView()) {
        if (auto it = local_scope.types.find(type_name.text); it != local_scope.types.end())
            return it->second.type;
    }

    throw SemanticError{"Undeclared type: " + type_name.text, type_name.span};
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

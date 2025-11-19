#include "symbol_table.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/ast.hpp"
#include "utils.hpp"

#include <cassert>
#include <cstddef>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

void SymbolTable::addLocalVariable(Identifier name, const Type& type) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.variables.contains(name.text))
        throw SemanticError{"Duplicate variable declaration: " + name.text, name.span};
    last_scope.variables.emplace(std::move(name.text), resolveType(type));
}

void SymbolTable::addLocalType(Identifier name, const Type& type) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.types.contains(name.text))
        throw SemanticError{"Duplicate type declaration: " + name.text, name.span};

    TypeId type_id = std::visit(overloaded{
                                    [](const parser::IntegerType&) { return IntegerTypeId; },
                                    [](const parser::RealType&) { return RealTypeId; },
                                    [](const parser::BoolType&) { return BooleanTypeId; },
                                    [this, &name](const parser::ArrayType& at) {
                                        types.emplace_back(createArrayTypeInfo(at), std::move(name.text));
                                        return types.size() - 1;
                                    },
                                    [this, &name](const parser::RecordType& rt) {
                                        types.emplace_back(createRecordTypeInfo(rt), std::move(name.text));
                                        return types.size() - 1;
                                    },
                                    [this](const parser::Identifier& type_name) { return resolveType(type_name); },
                                },
                                type);
    last_scope.types.emplace(std::move(name.text), type_id);
}

TypeId SymbolTable::getVariableType(const Identifier& name) const {
    for (const Scope& scope : getScopesView()) {
        if (auto it = scope.variables.find(name.text); it != scope.variables.end()) {
            return it->second.type;
        }
    }
    throw SemanticError{"Undeclared variable: " + name.text, name.span};
}

TypeId SymbolTable::resolveType(const Identifier& type_name) {
    for (const Scope& local_scope : getScopesView()) {
        if (auto it = local_scope.types.find(type_name.text); it != local_scope.types.end())
            return it->second;
    }
    throw SemanticError{"Undeclared type: " + type_name.text, type_name.span};
}

TypeId SymbolTable::resolveType(const Type& type) {
    return std::visit(overloaded{
                          [](const parser::IntegerType&) { return IntegerTypeId; },
                          [](const parser::RealType&) { return RealTypeId; },
                          [](const parser::BoolType&) { return BooleanTypeId; },
                          [this](const parser::ArrayType& at) {
                              TypeId type_id = types.size();
                              types.emplace_back(createArrayTypeInfo(at), std::format("unnamed{}", type_id));
                              return type_id;
                          },
                          [this](const parser::RecordType& rt) {
                              TypeId type_id = types.size();
                              types.emplace_back(createRecordTypeInfo(rt), std::format("unnamed{}", type_id));
                              return type_id;
                          },
                          [this](const parser::Identifier& type_name) { return resolveType(type_name); },
                      },
                      type);
}

ArrayTypeInfo SymbolTable::createArrayTypeInfo(const parser::ArrayType& type) {
    assert(type.computed_size != static_cast<std::size_t>(-1) && "Size of the array was not computed");
    ArrayTypeInfo info{.size = type.computed_size, .element_type = resolveType(*type.element_type)};
    return info;
}

RecordTypeInfo SymbolTable::createRecordTypeInfo(const parser::RecordType& type) {
    RecordTypeInfo info{};
    for (const VariableDeclaration& var_decl : type.fields) {
        assert(var_decl.resolved_type != static_cast<std::size_t>(-1) && "Variable's type was not resolved");
        info.fields.emplace(var_decl.name.text, var_decl.resolved_type);
    }
    return info;
}

const TypeInfo& SymbolTable::getTypeInfo(TypeId type_id) const {
    assert(type_id >= 0 && "Unresolved type given");
    return types[type_id];
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
            types[scope.types.at(identifier)].used = true;
            return;
        }
    }
}

void SymbolTable::markRoutineUsed(const std::string& identifier) {
    if (auto it = routines.find(identifier); it != routines.end())
        it->second.used = true;
}

} // namespace analyzer

#include "symbol_table.hpp"

#include "analyzer/semantic_error.hpp"
#include "parser/ast.hpp"
#include "utils.hpp"

#include <cassert>
#include <concepts>
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

void SymbolTable::ensureVarExists(const ModifiablePrimary& mp) const {
    for (const Scope& scope : getScopesView()) {
        if (scope.variables.contains(mp.variable.text))
            return;
    }
    throw SemanticError{"Undeclared variable: " + mp.variable.text, mp.variable.span};
}

void SymbolTable::ensureTypeExists(const Identifier& type_name) const {
    for (const Scope& scope : getScopesView()) {
        if (scope.types.contains(type_name.text))
            return;
    }
    throw SemanticError{"Undeclared type: " + type_name.text, type_name.span};
}

void SymbolTable::ensureTypeExists(const Type& type) const {
    if (std::holds_alternative<Identifier>(type))
        ensureTypeExists(std::get<Identifier>(type));
}

void SymbolTable::addLocalVariable(Identifier name, TypeId type) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.variables.contains(name.text))
        throw SemanticError{"Duplicate variable declaration: " + name.text, name.span};
    last_scope.variables.emplace(std::move(name.text), type);
}

void SymbolTable::addLocalTypeDeclaration(Identifier name, TypeId type_id) {
    Scope& last_scope = getCurrentScope();
    if (last_scope.types.contains(name.text))
        throw SemanticError{"Duplicate type declaration: " + name.text, name.span};

    TypeInfo& type_info = types[type_id];
    // fix anonymous creation from resolveType()
    if (std::holds_alternative<RecordTypeInfo>(type_info.definition))
        type_info.name = name.text;

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

namespace {

std::string encodeArrayTypeName(const ArrayTypeInfo& array) {
    return std::format("{}[{}]", array.element_type, array.size);
}

} // namespace

TypeId SymbolTable::resolveType(const Type& type) {
    return std::visit(overloaded{
                          [](const parser::IntegerType&) { return IntegerTypeId; },
                          [](const parser::RealType&) { return RealTypeId; },
                          [](const parser::BoolType&) { return BooleanTypeId; },
                          [this](const parser::ArrayType& at) {
                              ArrayTypeInfo info = createArrayTypeInfo(at);
                              std::string encoded_type_name = encodeArrayTypeName(info);
                              if (getGlobalScope().types.contains(encoded_type_name))
                                  return getGlobalScope().types.at(encoded_type_name);

                              TypeId type_id = types.size();
                              types.emplace_back(
                                  info, std::format("array[{}] of {}", info.size, getTypeInfo(info.element_type).name));
                              getGlobalScope().types.emplace(encoded_type_name, type_id);
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
    assert(type_id != static_cast<TypeId>(-1) && "Unresolved type given");
    return types[type_id];
}

bool SymbolTable::isConvertibleTo(TypeId from, TypeId to) const {
    const TypeInfo& from_info = getTypeInfo(from);
    const TypeInfo& to_info = getTypeInfo(to);
    return std::visit(overloaded{
                          [from, to](const RecordTypeInfo&, const RecordTypeInfo&) { return from == to; },
                          [from, to](const ArrayTypeInfo&, const ArrayTypeInfo&) { return from == to; },
                          [](IntegerTypeInfo, RealTypeInfo) { return true; },
                          [](IntegerTypeInfo, BooleanTypeInfo) { return true; },
                          [](RealTypeInfo, IntegerTypeInfo) { return true; },
                          [](RealTypeInfo, BooleanTypeInfo) { return false; },
                          [](BooleanTypeInfo, IntegerTypeInfo) { return true; },
                          [](BooleanTypeInfo, RealTypeInfo) { return true; },
                          []<class T1, class T2>(const T1&, const T2&) { return std::same_as<T1, T2>; },
                      },
                      from_info.definition,
                      to_info.definition);
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

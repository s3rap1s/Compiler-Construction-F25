#pragma once

#include "parser/ast.hpp"
#include "utils.hpp"

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace analyzer {

using namespace parser;

struct SymbolTable;

struct VarInfo {
    TypeId type = -1;
    std::optional<Expression> initial_value;
    bool used = false;
};

struct RoutineInfo {
    std::vector<TypeId> parameters;
    std::optional<TypeId> return_type;
    Span span_of_declaration;
    bool defined;
    bool used = false;
};

struct Scope {
    const Block* parent;
    std::unordered_map<std::string, VarInfo> variables;
    std::unordered_map<std::string, TypeId> types;

    explicit Scope(const Block* parent) : parent{parent} {}
};

/* ============
 * Types
 * ============
 */
struct IntegerTypeInfo {};

struct RealTypeInfo {};

struct BooleanTypeInfo {};

struct RecordTypeInfo {
    std::vector<std::pair<std::string, TypeId>> fields;
};

struct ArrayTypeInfo {
    std::size_t size;
    TypeId element_type;
};

struct TypeInfo {
    using Definition = std::variant<IntegerTypeInfo, RealTypeInfo, BooleanTypeInfo, RecordTypeInfo, ArrayTypeInfo>;

    Definition definition;
    std::string name;
    bool used = false;

    TypeInfo(Definition definition, std::string name) : definition{std::move(definition)}, name{std::move(name)} {}
};

struct SymbolTable {
  private:
    std::vector<TypeInfo> types{
        {IntegerTypeInfo{}, "integer"}, {RealTypeInfo{}, "real"}, {BooleanTypeInfo{}, "boolean"}};

    std::unordered_map<std::string, RoutineInfo> routines;
    std::unordered_set<std::string> for_loop_variables;

    std::unordered_map<const parser::Block*, Scope> scopes; // initialize with global scope
    const parser::Block* current_block = nullptr;

    template <bool Const>
    struct ScopeIterator {
        MaybeConst<Const, std::unordered_map<const Block*, Scope>>* scopes;
        const Block* current_block;

        MaybeConst<Const, Scope>& operator*() const {
            return scopes->find(current_block)->second;
        }

        ScopeIterator& operator++() {
            const Block* parent = (**this).parent;
            if (current_block == nullptr && parent == nullptr)
                scopes = nullptr;
            else
                current_block = parent;
            return *this;
        }

        bool operator==(std::default_sentinel_t /*unused*/) const {
            return scopes == nullptr;
        }
    };

    // inspired by std::ranges::views
    template <bool Const>
    struct ScopesView {
        MaybeConst<Const, SymbolTable>* table;

        [[nodiscard]] ScopeIterator<Const> begin() const {
            return ScopeIterator<Const>{.scopes = &table->scopes, .current_block = table->current_block};
        }

        [[nodiscard]] static std::default_sentinel_t end() {
            return {};
        }
    };

  public:
    static constexpr TypeId IntegerTypeId = 0;
    static constexpr TypeId RealTypeId = 1;
    static constexpr TypeId BooleanTypeId = 2;

    SymbolTable() {
        scopes.emplace(nullptr, Scope{nullptr});
    }

    std::unordered_map<std::string, RoutineInfo>& getRoutines();
    const std::unordered_map<std::string, RoutineInfo>& getRoutines() const;
    std::unordered_map<const Block*, Scope>& getScopes();
    std::unordered_set<std::string>& getForLoopVariables();

    ScopesView<false> getScopesView();
    ScopesView<true> getScopesView() const;

    Scope& getGlobalScope();
    Scope& getCurrentScope();

    void pushScope(const Block& block);
    void popScope(const Block& block);

    void ensureVarExists(const ModifiablePrimary& mp) const;
    void ensureTypeExists(const Identifier& type_name) const;
    void ensureTypeExists(const Type& type) const;

    void addLocalVariable(Identifier name, TypeId type);
    void addLocalTypeDeclaration(Identifier name, TypeId type_id);

    ArrayTypeInfo createArrayTypeInfo(const parser::ArrayType& type);
    static RecordTypeInfo createRecordTypeInfo(const parser::RecordType& type);
    const TypeInfo& getTypeInfo(TypeId type_id) const;
    TypeId resolveType(const Type& type);
    TypeId resolveType(const Identifier& type);
    TypeId getVariableType(const Identifier& name) const;

    bool isConvertibleTo(TypeId from, TypeId to) const;

    void markVarUsed(const std::string& identifier);
    void markTypeUsed(const std::string& identifier);
    void markRoutineUsed(const std::string& identifier);
};

} // namespace analyzer

#pragma once

#include "parser/declarations.hpp"
#include "parser/expressions.hpp"
#include "parser/statements.hpp"
#include "parser/types.hpp"
#include "utils.hpp"

#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace analyzer {

using namespace parser;

struct SymbolTable;

struct VarInfo {
    const Type* type;
    bool used;
};

struct TypeInfo {
    std::reference_wrapper<const Type> type;
    bool used;
};

struct RoutineInfo {
    std::reference_wrapper<const RoutineDeclaration> declaration;
    bool defined;
    bool used;
};

struct Scope {
    const Block* parent;
    std::unordered_map<std::string, VarInfo> variables;
    std::unordered_map<std::string, TypeInfo> types;

    explicit Scope(const Block* parent) : parent{parent} {}
};

struct SymbolTable {
  private:
    std::unordered_map<std::string, RoutineInfo> routines;
    std::unordered_set<std::string> for_loop_variables;
    std::unordered_map<const parser::Block*, Scope> scopes;
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
    std::unordered_map<std::string, RoutineInfo>& getRoutines();
    std::unordered_map<const Block*, Scope>& getScopes();
    std::unordered_set<std::string>& getForLoopVariables();

    ScopesView<false> getScopesView();
    ScopesView<true> getScopesView() const;

    Scope& getGlobalScope();
    Scope& getCurrentScope();

    void pushScope(const Block& block);
    void popScope(const Block& block);

    bool varExists(const ModifiablePrimary& mp) const;
    bool typeExists(const std::string& type_name) const;
    bool typeExists(const Type& type) const;

    void addLocalVariable(const VariableDeclaration& vd);
    void addLocalParameter(const ParameterDeclaration& pd);
    void addLocalType(const TypeDeclaration& td);

    const Type& getVariableType(const ModifiablePrimary& mp) const;
    const Type& resolveType(const Type& type) const;

    void markVarUsed(const std::string& identifier);
    void markTypeUsed(const std::string& identifier);
    void markRoutineUsed(const std::string& identifier);
};

} // namespace analyzer

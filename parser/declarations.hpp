#pragma once

#include "expressions.hpp"
#include "statements.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace parser {

struct VariableDeclaration : AstNode { //NOLINT(*init*)
    std::string identifier;
    std::optional<Type> type;
    std::optional<Expression> value;
    // mamoi klyanus', ne budet dva optional pustimi. (c) Maxim Fomin

    VariableDeclaration() = default;

    template<typename T, typename E>
    VariableDeclaration(std::string identifier, T&& type, E&& value) : // NOLINT(*init*)
        identifier(std::move(identifier)), type(std::forward<T>(type)), value(std::forward<E>(value)) {}
};

struct ParameterDecalration : AstNode {
    std::string identifier;
    Type type;
};

struct RoutineDeclaration : AstNode {
    std::string identifier;
    std::vector<ParameterDecalration> parameters;
    std::optional<std::variant<Block, Expression>> body;
    std::optional<Type> return_type;
};

struct TypeDeclaration : AstNode {
    std::string identifier;
    Type type;
};

struct Program {
    std::vector<std::variant<VariableDeclaration, TypeDeclaration, RoutineDeclaration>> declarations;
};

} // namespace parser

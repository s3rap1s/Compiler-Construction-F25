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

struct VariableDeclaration : AstNode { // NOLINT(*init*)
    std::string identifier;
    std::optional<Type> type;
    std::optional<Expression> value;
    // mamoi klyanus', ne budet dva optional pustimi. (c) Maxim Fomin

    VariableDeclaration() = default;

    VariableDeclaration(std::string identifier, // NOLINT(*init*)
                        std::optional<Type> type,
                        std::optional<Expression> value)
        : identifier{std::move(identifier)}, type{std::move(type)}, value{std::move(value)} {}
};

struct ParameterDeclaration : AstNode {
    std::string identifier;
    Type type;
};

struct RoutineDeclaration : AstNode {
    std::string identifier;
    std::vector<ParameterDeclaration> parameters;
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

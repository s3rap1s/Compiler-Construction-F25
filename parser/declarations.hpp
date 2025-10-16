#pragma once

#include "expressions.hpp"
#include "statements.hpp"
#include "types.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace parser {

struct VariableDeclaration {
    std::string identifier;
    std::optional<Type> type;
    std::optional<Expression> value;
    // mamoi klyanus', ne budet dva optional pustimi. (c) Maxim Fomin
};

struct ParameterDecalration {
    std::string identifier;
    Type type;
};

struct RoutineDeclaration {
    std::string identifier;
    std::vector<ParameterDecalration> parameters;
    std::optional<std::variant<Block, Expression>> body;
    std::optional<Type> return_type;
};

struct TypeDeclaration {
    std::string identifier;
    Type type;
};

struct Program {
    std::vector<std::variant<VariableDeclaration, TypeDeclaration, RoutineDeclaration>> declarations;
};

} // namespace parser

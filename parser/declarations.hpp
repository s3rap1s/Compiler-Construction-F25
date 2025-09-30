#pragma once

#include "types.hpp"
#include "expressions.hpp"
#include "statements.hpp"

#include <optional>
#include <string>
#include <variant>

namespace parser {

class VariableDeclaration{
    std::string identifier;
    std::optional<Type> type;
    std::optional<Expression> value; 
    // mamoi klyanus', ne budet dva optional pustimi. c Maxim Fomin
};

class ParameterDecalration {
    std::string identifier;
    Type type;
};

class RoutineDeclaration{
    std::string identifier;
    std::vector<ParameterDecalration> parameters;
    std::optional<std::variant<Block, Expression>> body;
    Type return_type;
};

class TypeDeclaration{
    std::string identifier;
    Type type;
};

} // namespace parser

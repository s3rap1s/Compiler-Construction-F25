#pragma once

#include "parser/expressions.hpp"
#include "parser/ast.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace parser {

struct IntegerType : AstNode {};
struct RealType : AstNode {};
struct BoolType : AstNode {};
struct RecordType;
struct ArrayType;
using Type = std::variant<IntegerType, RealType, BoolType, RecordType, ArrayType, std::string>;

struct VariableDeclaration;
struct RecordType : AstNode {
    std::vector<std::shared_ptr<VariableDeclaration>> fields;
};

struct ArrayType : AstNode {
    std::optional<Expression> size;
    std::shared_ptr<Type> element_type;
};

} // namespace parser

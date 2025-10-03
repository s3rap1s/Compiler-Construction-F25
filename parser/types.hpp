#pragma once

#include "parser/expressions.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace parser {

struct IntegerType {};
struct RealType {};
struct BoolType {};
struct RecordType;
struct ArrayType;
using Type = std::variant<IntegerType, RealType, BoolType, RecordType, ArrayType, std::string>;

struct VariableDeclaration;
struct RecordType {
    std::vector<VariableDeclaration> fields;
};

struct ArrayType {
    std::optional<Expression> size;
    std::unique_ptr<Type> element_type;
};

} // namespace parser

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
    std::vector<std::shared_ptr<VariableDeclaration>> fields;

    RecordType() = default;
    explicit RecordType(std::vector<std::shared_ptr<VariableDeclaration>>&& fields);
};

struct ArrayType {
    std::optional<Expression> size;
    std::shared_ptr<Type> element_type;
};

} // namespace parser

#pragma once

#include <string>
#include <variant>
#include <vector>

namespace parser {

struct VariableDeclaration;
struct IntegerType {};
struct RealType {};
struct BoolType {};
struct RecordType;
struct ArrayType;
using Type = std::variant<IntegerType, RealType, BoolType, RecordType, ArrayType, std::string>;

struct RecordType {
    std::vector<VariableDeclaration> fields;
};

struct ArrayType {
    std::vector<Type> elements;
};

} // namespace parser

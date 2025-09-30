#pragma once

#include <string>
#include <variant>
#include <vector>

namespace parser {

class VariableDeclaration;
class IntegerType{};
class RealType{};
class BoolType{};
class RecordType;
class ArrayType;
using Type = std::variant<IntegerType, RealType, BoolType, RecordType, ArrayType, std::string>;

class RecordType {
    std::vector<VariableDeclaration> fields;
};

class ArrayType {
    std::vector<Type> elements;
};

} // namespace parser

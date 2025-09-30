#pragma once

#include <string>
#include <variant>

struct IntegerLiteralError {
    std::string literal;
};

struct RealLiteralError {
    std::string literal;
};

struct UnknownToken {
    std::string token;
};

using LexingError = std::variant<IntegerLiteralError, RealLiteralError, UnknownToken>;

#pragma once

#include "tokens.hpp"

namespace lexer {

struct TokenPrinter {
    static void print(const Token& token);
};

} // namespace lexer

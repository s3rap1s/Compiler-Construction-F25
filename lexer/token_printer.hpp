#pragma once

#include "tokens.hpp"

#include <string>

namespace lexer {

std::string representSyntaxPart(SyntaxPart sp);
std::string representLiteral(const Literal& literal);
std::string representToken(const Token& token);

void print(const Token& token);

} // namespace lexer

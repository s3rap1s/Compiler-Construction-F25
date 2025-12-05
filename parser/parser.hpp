#pragma once

#include "lexer/lexer.hpp"
#include "parser/ast.hpp"
#include "parser/syntax_error.hpp"

#include <expected>

namespace parser {

std::expected<Program, SyntaxError> parse(lexer::Lexer& lexer);

} // namespace parser

#pragma once

#include "parser/declarations.hpp"
#include "analyzer/semantic_error.hpp"

#include <expected>

namespace analyzer {

std::expected<parser::Program, SemanticError> parse(parser::Program ast);

} // namespace analyzer

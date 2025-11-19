#pragma once

#include "analyzer/semantic_error.hpp"
#include "parser/ast.hpp"

#include <optional>
#include <string_view>

namespace analyzer {

std::optional<SemanticError> analyze(parser::Program& ast, std::string_view entry_point);

} // namespace analyzer

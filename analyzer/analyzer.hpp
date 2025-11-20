#pragma once

#include "analyzer/semantic_error.hpp"
#include "analyzer/symbol_table.hpp"
#include "parser/ast.hpp"

#include <expected>
#include <string_view>

namespace analyzer {

std::expected<SymbolTable, SemanticError> analyze(parser::Program& ast, std::string_view entry_point);

} // namespace analyzer
